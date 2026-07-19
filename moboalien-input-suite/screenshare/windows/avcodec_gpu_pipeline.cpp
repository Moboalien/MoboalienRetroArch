#include "avcodec_gpu_pipeline.h"
#include "video_config.h"
#include "utils.h"
#include <cstdlib>
#include <sstream>
#include <unknwn.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_qsv.h>
#include <d3d11.h>

static const char* TAG = "AvcodecGpuPipeline";

#ifndef FF_PROFILE_H264_BASELINE
#define FF_PROFILE_H264_BASELINE 66
#endif

// Forward declaration
static void verify_qsv_texture_ffmpeg5(AVBufferRef* qsv_frames_ref);

AvcodecGpuPipeline::AvcodecGpuPipeline(Platform* platform) : AvcodecPipeline(platform) {}

AvcodecGpuPipeline::~AvcodecGpuPipeline() {
#ifdef USE_FFMPEG
    CleanupRingBuffer();
    if (m_d3dContext) {
        m_d3dContext->Release();
        m_d3dContext = nullptr;
    }
    if (m_d3dDevice) {
        m_d3dDevice->Release();
        m_d3dDevice = nullptr;
    }
    if (m_hw_device_ctx) { av_buffer_unref(&m_hw_device_ctx); m_hw_device_ctx = nullptr; }
    if (m_hw_frames_ctx) { av_buffer_unref(&m_hw_frames_ctx); m_hw_frames_ctx = nullptr; }
    if (m_d3d11_hw_dev_ctx) { av_buffer_unref(&m_d3d11_hw_dev_ctx); m_d3d11_hw_dev_ctx = nullptr; }
    if (m_d3d11_frames_ctx) { av_buffer_unref(&m_d3d11_frames_ctx); m_d3d11_frames_ctx = nullptr; }
    if (qsv_hw_dev_ctx) { av_buffer_unref(&qsv_hw_dev_ctx); qsv_hw_dev_ctx = nullptr; }
#endif
}

bool AvcodecGpuPipeline::SetupNV12TextureWithUAVs(NV12TextureSet* slot) {
    if (!slot) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width  = static_cast<UINT>(m_width);
    desc.Height = static_cast<UINT>(m_height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;          // NOT 2!
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    
    HRESULT hr = m_d3dDevice->CreateTexture2D(&desc, nullptr, &slot->nv12Texture);
    if (FAILED(hr)) {
        LOGE(TAG, "Failed to create NV12 texture, hr=0x" + std::to_string(hr));
        return false;
    }
    LOGI(TAG, "NV12 texture created: size=" + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " bindFlags=0x" + std::to_string(desc.BindFlags));

    D3D11_QUERY_DESC queryDesc = {};
    queryDesc.Query = D3D11_QUERY_EVENT;
    if (FAILED(m_d3dDevice->CreateQuery(&queryDesc, &slot->copyQuery))) {
        LOGE(TAG, "Failed to create copy query");
        return false;
    }

    return true;
}

bool AvcodecGpuPipeline::InitializeD3D11Device() {
    int ret = av_hwdevice_ctx_create(&m_d3d11_hw_dev_ctx, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
    if (ret < 0) {
        LOGE(TAG, "Failed to create D3D11 device");
        return false;
    }

    AVHWDeviceContext* d3d11_ctx = (AVHWDeviceContext*)m_d3d11_hw_dev_ctx->data;
    AVD3D11VADeviceContext* d3d11_hwctx = (AVD3D11VADeviceContext*)d3d11_ctx->hwctx;
    m_d3dDevice = d3d11_hwctx->device;
    m_d3dDevice->AddRef();
    m_d3dDevice->GetImmediateContext(&m_d3dContext);

    // Set GPU thread priority to high
    IDXGIDevice2* pDXGIDevice = nullptr;
    HRESULT hr = m_d3dDevice->QueryInterface(__uuidof(IDXGIDevice2), (void**)&pDXGIDevice);
    if (SUCCEEDED(hr) && pDXGIDevice) {
        // Priority levels range from -7 (lowest) to 7 (highest).
        hr = pDXGIDevice->SetGPUThreadPriority(7);
        if (SUCCEEDED(hr)) {
            LOGI(TAG, "Successfully set GPU thread priority to high (7).");
        } else {
            LOGW(TAG, "Failed to set GPU thread priority, hr=0x" + std::to_string(hr));
        }
        pDXGIDevice->Release();
        pDXGIDevice = nullptr;
    } else {
        LOGW(TAG, "Failed to query IDXGIDevice2 interface, cannot set GPU thread priority.");
    }

    return true;
}

bool AvcodecGpuPipeline::InitializeQSVDevice() {
    int ret = av_hwdevice_ctx_create_derived(&qsv_hw_dev_ctx, AV_HWDEVICE_TYPE_QSV, m_d3d11_hw_dev_ctx, 0);
    if (ret < 0) {
        LOGE(TAG, "Failed to derive QSV device from D3D11");
        return false;
    }
    return true;
}

bool AvcodecGpuPipeline::InitializeFramesContext(int width, int height) {
    // Create QSV frames context
    m_hw_frames_ctx = av_hwframe_ctx_alloc(qsv_hw_dev_ctx);
    if (!m_hw_frames_ctx) return false;

    AVHWFramesContext* qsv_f = (AVHWFramesContext*)m_hw_frames_ctx->data;
    qsv_f->format = AV_PIX_FMT_QSV;
    qsv_f->sw_format = AV_PIX_FMT_NV12;
    qsv_f->width = width;
    qsv_f->height = height;
    qsv_f->initial_pool_size = 64; // nv12 slots + encoder internal surfaces (async_depth + lookahead)

    AVQSVFramesContext* qsv_hwctx = (AVQSVFramesContext*)qsv_f->hwctx;
    qsv_hwctx->frame_type = MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET | MFX_MEMTYPE_FROM_VPPOUT;

    if (av_hwframe_ctx_init(m_hw_frames_ctx) < 0) {
        LOGE(TAG, "Failed to init QSV frames context");
        return false;
    }

    verify_qsv_texture_ffmpeg5(m_hw_frames_ctx);

    // Create D3D11 frames context for mapping
    m_d3d11_frames_ctx = av_hwframe_ctx_alloc(m_d3d11_hw_dev_ctx);
    if (!m_d3d11_frames_ctx) return false;

    AVHWFramesContext* d3d11_f = (AVHWFramesContext*)m_d3d11_frames_ctx->data;
    d3d11_f->format = AV_PIX_FMT_D3D11;
    d3d11_f->sw_format = AV_PIX_FMT_NV12;
    d3d11_f->width = width;
    d3d11_f->height = height;
    d3d11_f->initial_pool_size = 0;

    if (av_hwframe_ctx_init(m_d3d11_frames_ctx) < 0) {
        LOGE(TAG, "Failed to init D3D11 frames context for mapping");
        return false;
    }

    return true;
}

#pragma warning(push)
#pragma warning(disable: 4509) // nonstandard extension used: function uses SEH and object has destructor

static HRESULT TryGetTextureDesc(void* mem_id, D3D11_TEXTURE2D_DESC* desc) {
    HRESULT hr = E_FAIL;
    __try {
        ID3D11Texture2D* tex = (ID3D11Texture2D*)mem_id;
        tex->GetDesc(desc);
        hr = S_OK;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        hr = E_FAIL;
    }
    return hr;
}

static HRESULT TryGetPairTextureDesc(void* mem_id, D3D11_TEXTURE2D_DESC* desc, void** out_first, void** out_second) {
    typedef struct {
        void* first;
        void* second;
    } HDLPair;
    
    HRESULT hr = E_FAIL;
    __try {
        HDLPair* pair = (HDLPair*)mem_id;
        if (pair->first) {
            ID3D11Texture2D* tex = (ID3D11Texture2D*)pair->first;
            tex->GetDesc(desc);
            *out_first = pair->first;
            *out_second = pair->second;
            hr = S_OK;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        hr = E_FAIL;
    }
    return hr;
}

static void verify_qsv_texture_ffmpeg5(AVBufferRef* qsv_frames_ref)
{
    AVHWFramesContext* fctx = (AVHWFramesContext*)qsv_frames_ref->data;
    AVQSVFramesContext* qsv_fctx = (AVQSVFramesContext*)fctx->hwctx;
    
    LOGI(TAG, "=== FFmpeg 5.x QSV Verification ===");
    LOGI(TAG, "nb_surfaces: " + std::to_string(qsv_fctx->nb_surfaces));
    LOGI(TAG, "frame_type: " + std::to_string(qsv_fctx->frame_type));
    
    // Check surfaces array directly from context
    LOGI(TAG, "=== Checking surfaces array ===");
    for (int i = 0; i < qsv_fctx->nb_surfaces && i < 3; i++) {
        mfxFrameSurface1* surf = &qsv_fctx->surfaces[i];
        LOGI(TAG, "Surface[" + std::to_string(i) + "]:");
        LOGI(TAG, "  MemId: " + std::to_string((uintptr_t)surf->Data.MemId));
        LOGI(TAG, "  FourCC: 0x" + std::to_string(surf->Info.FourCC));
    }
    
    // Get a frame
    LOGI(TAG, "=== Getting hw frame ===");
    AVFrame* hw = av_frame_alloc();
    int ret = av_hwframe_get_buffer(qsv_frames_ref, hw, 0);
    LOGI(TAG, "av_hwframe_get_buffer returned: " + std::to_string(ret));
    
    if (ret < 0) {
        av_frame_free(&hw);
        return;
    }
    
    LOGI(TAG, "data[3]: " + std::to_string((uintptr_t)hw->data[3]));
    
    mfxFrameSurface1* surf = (mfxFrameSurface1*)hw->data[3];
    LOGI(TAG, "Surface MemId: " + std::to_string((uintptr_t)(surf ? surf->Data.MemId : NULL)));
    
    // Do software upload
    LOGI(TAG, "=== Doing software upload ===");
    AVFrame* sw = av_frame_alloc();
    sw->format = AV_PIX_FMT_NV12;
    sw->width = fctx->width;
    sw->height = fctx->height;
    av_frame_get_buffer(sw, 32);
    memset(sw->data[0], 16, sw->linesize[0] * fctx->height);
    memset(sw->data[1], 128, sw->linesize[1] * fctx->height / 2);
    
    ret = av_hwframe_transfer_data(hw, sw, 0);
    LOGI(TAG, "av_hwframe_transfer_data returned: " + std::to_string(ret));
    av_frame_free(&sw);
    
    // Check again after upload
    LOGI(TAG, "=== After upload ===");
    LOGI(TAG, "data[3]: " + std::to_string((uintptr_t)hw->data[3]));
    
    surf = (mfxFrameSurface1*)hw->data[3];
    void* mem_id = surf ? surf->Data.MemId : NULL;
    LOGI(TAG, "Surface MemId: " + std::to_string((uintptr_t)mem_id));
    
    if (!mem_id) {
        LOGI(TAG, "*** MemId is NULL - GPU copy not possible ***");
        av_frame_free(&hw);
        return;
    }
    
    // Try different interpretations of MemId
    LOGI(TAG, "=== Trying to interpret MemId ===");
    
    // Method 1: Direct texture pointer (older style)
    LOGI(TAG, "Trying as direct ID3D11Texture2D*...");
    D3D11_TEXTURE2D_DESC desc;
    
    HRESULT hr = TryGetTextureDesc(mem_id, &desc);
    
    if (SUCCEEDED(hr)) {
        LOGI(TAG, "*** SUCCESS: Direct texture pointer! ***");
        LOGI(TAG, "  Width: " + std::to_string(desc.Width));
        LOGI(TAG, "  Height: " + std::to_string(desc.Height));
        LOGI(TAG, "  Format: " + std::to_string(desc.Format));
        LOGI(TAG, "  ArraySize: " + std::to_string(desc.ArraySize));
        LOGI(TAG, "Use: context->CopyResource(tex_direct, your_nv12);");
        av_frame_free(&hw);
        return;
    }
    
    // Method 2: mfxHDLPair (newer style)
    LOGI(TAG, "Trying as mfxHDLPair*...");
    
    void* pair_first = nullptr;
    void* pair_second = nullptr;
    hr = TryGetPairTextureDesc(mem_id, &desc, &pair_first, &pair_second);
    
    if (SUCCEEDED(hr)) {
        LOGI(TAG, "*** SUCCESS: mfxHDLPair pointer! ***");
        LOGI(TAG, "  Texture: " + std::to_string((uintptr_t)pair_first));
        LOGI(TAG, "  Index: " + std::to_string((long long)(intptr_t)pair_second));
        LOGI(TAG, "  Width: " + std::to_string(desc.Width));
        LOGI(TAG, "  Height: " + std::to_string(desc.Height));
        LOGI(TAG, "  Format: " + std::to_string(desc.Format));
        LOGI(TAG, "  ArraySize: " + std::to_string(desc.ArraySize));
        LOGI(TAG, "Use: context->CopySubresourceRegion(tex, index, ...);");
        av_frame_free(&hw);
        return;
    }
    
    // Method 3: Check if it's a handle
    LOGI(TAG, "Trying as HANDLE...");
    LOGI(TAG, "  Raw value: 0x" + std::to_string((unsigned long long)(uintptr_t)mem_id));
    
    LOGI(TAG, "*** Could not interpret MemId ***");
    std::string bytes_str = "Raw bytes of MemId area: ";
    uint8_t* bytes = (uint8_t*)mem_id;
    for (int i = 0; i < 32; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02x ", bytes[i]);
        bytes_str += buf;
    }
    LOGI(TAG, bytes_str);
    
    av_frame_free(&hw);
}

#pragma warning(pop)

bool AvcodecGpuPipeline::InitializeRingBuffer() {
    m_nv12Pool.reserve(nv12PoolSize);
    for (int i = 0; i < nv12PoolSize; ++i) {
        if (!CreateNV12Slot()) {
            LOGE(TAG, "InitializeRingBuffer: CreateNV12Slot failed at index " + std::to_string(i));
            return false;
        }
    }
    return true;
}

int AvcodecGpuPipeline::GetCurrentNv12SlotCount() const {
    return static_cast<int>(m_nv12Pool.size() + m_pendingRelease.size());
}

bool AvcodecGpuPipeline::CreateNV12Slot() {
    int currentCount = GetCurrentNv12SlotCount();
    if (currentCount >= maxNv12PoolSize) {
        LOGW(TAG, "CreateNV12Slot: reached max slot count " + std::to_string(maxNv12PoolSize) + ", skipping creation");
        return false;
    }

    NV12TextureSet slot;
    slot.frame = av_frame_alloc();
    if (!slot.frame) {
        LOGE(TAG, "CreateNV12Slot: av_frame_alloc failed");
        return false;
    }
    slot.frame->format = AV_PIX_FMT_QSV;

    if (!SetupNV12TextureWithUAVs(&slot)) {
        LOGE(TAG, "CreateNV12Slot: Failed to setup UAVs");
        av_frame_free(&slot.frame);
        return false;
    }

    m_nv12Pool.push_back(std::move(slot));
    LOGI(TAG, "CreateNV12Slot: dynamic slot created, pool size=" + std::to_string(m_nv12Pool.size()));
    return true;
}

bool AvcodecGpuPipeline::InitializeEncoder(const AVCodec* codec, int quality) {
    m_codecContext->hw_device_ctx = av_buffer_ref(qsv_hw_dev_ctx);
    m_codecContext->hw_frames_ctx = av_buffer_ref(m_hw_frames_ctx);
    m_codecContext->pix_fmt = AV_PIX_FMT_QSV;

    av_opt_set(m_codecContext->priv_data, "look_ahead", "0", 0);
    av_opt_set(m_codecContext->priv_data, "async_depth", "2", 0);
    av_opt_set(m_codecContext->priv_data, "preset", "veryfast", 0);
    int h264Quality = MapQualityToH264(quality);
    LOGI(TAG, "Mapped quality " + std::to_string(quality) + " to H.264 QP " + std::to_string(h264Quality));
    // 1. Set Rate Control to Constant QP (Lowest overhead)
    av_opt_set(m_codecContext->priv_data, "rc", "cqp", 0);
    // 2. Set the Quality (Lower = Higher Quality, 20-25 is a good start)
    av_opt_set_int(m_codecContext->priv_data, "q", h264Quality, 0);
    
    m_codecContext->profile = FF_PROFILE_H264_BASELINE;

    if (avcodec_open2(m_codecContext, codec, nullptr) < 0) {
        LOGE(TAG, "Failed to open QSV encoder");
        return false;
    }

    const AVBitStreamFilter* filter = av_bsf_get_by_name("h264_mp4toannexb");
    if (!filter || av_bsf_alloc(filter, &m_bsf) < 0) return false;
    if (avcodec_parameters_from_context(m_bsf->par_in, m_codecContext) < 0) return false;
    m_bsf->time_base_in = m_codecContext->time_base;
    if (av_bsf_init(m_bsf) < 0) return false;

    m_pkt = av_packet_alloc();
    return true;
}

bool AvcodecGpuPipeline::Initialize(IScreenCapture* capture, int width, int height, int fps, int quality, float scale) {
#ifdef USE_FFMPEG
    m_captureWidth  = width;
    m_captureHeight = height;
    m_scale  = scale;
    m_width  = static_cast<int>(width  * m_scale) & ~15;
    m_height = static_cast<int>(height * m_scale) & ~15;

    const AVCodec* codec = avcodec_find_encoder_by_name("h264_qsv");
    if (!codec) return false;

    if (!InitializeCommon(codec, m_width, m_height, fps, quality)) return false;
    if (!InitializeD3D11Device()) return false;

    capture->SetCustomDevice(m_d3dDevice);

    if (!m_colorConverter.Initialize(m_d3dDevice, m_d3dContext)) {
        LOGE(TAG, "Failed to initialize color converter");
        return false;
    }

    if (!InitializeQSVDevice()) return false;
    if (!InitializeFramesContext(m_width, m_height)) return false;
    if (!InitializeEncoder(codec, quality)) return false;
    if (!InitializeRingBuffer()) return false;

    LOGI(TAG, "QSV encoder initialized successfully");
    LOGI(TAG, "Config: captureSize=" + std::to_string(width) + "x" + std::to_string(height)
        + " encodeSize=" + std::to_string(m_width) + "x" + std::to_string(m_height)
        + " fps=" + std::to_string(fps) + " scale=" + std::to_string(scale));
    return true;
#else
    return false;
#endif
}

bool AvcodecGpuPipeline::ConvertBGRAToNV12(ID3D11Texture2D* bgra, NV12TextureSet& slot) {
    D3D11_TEXTURE2D_DESC srcDesc;
    bgra->GetDesc(&srcDesc);
    if (!m_colorConverter.ConvertBGRAToNV12VideoProcessor(bgra, slot.nv12Texture, srcDesc.Width, srcDesc.Height, m_width, m_height)) {
        LOGE(TAG, "GPU NV12 conversion using Video Processor failed");
        return false;
    }
    return true;
}

void AvcodecGpuPipeline::CleanupSlot(NV12TextureSet& slot) {
    if (slot.copyQuery)   { slot.copyQuery->Release();   slot.copyQuery   = nullptr; }
    if (slot.nv12Texture) { slot.nv12Texture->Release(); slot.nv12Texture = nullptr; }
    if (slot.frame)       { av_frame_free(&slot.frame); }
}

void AvcodecGpuPipeline::CleanupRingBuffer() {
    for (auto& slot : m_nv12Pool)      CleanupSlot(slot);
    for (auto& slot : m_pendingRelease) CleanupSlot(slot);
    m_nv12Pool.clear();
    m_pendingRelease.clear();
}

bool AvcodecGpuPipeline::HandleDimensionChange(int newWidth, int newHeight) {
    LOGI(TAG, "Dimensions changed from " + std::to_string(m_width) + "x" + std::to_string(m_height) + 
              " to " + std::to_string(newWidth) + "x" + std::to_string(newHeight) + ", reinitializing");
    
    CleanupRingBuffer();
    
    // Close encoder
    if (m_bsf) { av_bsf_free(&m_bsf); m_bsf = nullptr; }
    if (m_codecContext) { avcodec_free_context(&m_codecContext); m_codecContext = nullptr; }
    if (m_pkt) { av_packet_free(&m_pkt); m_pkt = nullptr; }
    
    m_captureWidth  = newWidth;
    m_captureHeight = newHeight;
    m_width  = static_cast<int>(newWidth  * m_scale) & ~15;
    m_height = static_cast<int>(newHeight * m_scale) & ~15;
    
    if (m_hw_frames_ctx) { av_buffer_unref(&m_hw_frames_ctx); m_hw_frames_ctx = nullptr; }
    if (m_d3d11_frames_ctx) { av_buffer_unref(&m_d3d11_frames_ctx); m_d3d11_frames_ctx = nullptr; }
    
    const AVCodec* codec = avcodec_find_encoder_by_name("h264_qsv");
    if (!codec) return false;
    
    if (!InitializeCommon(codec, m_width, m_height, m_fps, m_quality)) return false;
    if (!InitializeFramesContext(m_width, m_height)) return false;
    if (!InitializeEncoder(codec, m_quality)) return false;
    if (!InitializeRingBuffer()) return false;
    
    return true;
}

AVFrame* AvcodecGpuPipeline::PrepareFrame(IScreenCapture* capture, int64_t& pts) {
    uint64_t captureTime = capture->GetLastCaptureTimestamp();
    if (captureTime > 0) {
        if (m_startTimeMs == 0) m_startTimeMs = captureTime;
        pts = static_cast<int64_t>(captureTime - m_startTimeMs);
    }
    ID3D11Texture2D* bgra = static_cast<ID3D11Texture2D*>(capture->GetCaptureTexture());
    if (!bgra) {
        LOGD(TAG, "PrepareFrame: No BGRA texture");
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC desc;
    bgra->GetDesc(&desc);
    if (desc.Width != static_cast<UINT>(m_captureWidth) || desc.Height != static_cast<UINT>(m_captureHeight)) {
        if (!HandleDimensionChange(desc.Width, desc.Height)) {
            return nullptr;
        }
    }

    // Pop free slot, submit VPP, copy to QSV surface
    DrainPendingRelease();
    if (m_nv12Pool.empty()) {
        if (!CreateNV12Slot()) {
            LOGE(TAG, "PrepareFrame: No free NV12 slot and dynamic creation failed, dropping frame");
        }
    }

    if (!m_nv12Pool.empty()) {
        NV12TextureSet newSlot = std::move(m_nv12Pool.back());
        m_nv12Pool.pop_back();
        if (ConvertBGRAToNV12(bgra, newSlot)) {
            newSlot.frame->pts = pts;
        } else {
            LOGE(TAG, "PrepareFrame: VPP conversion failed");
            m_nv12Pool.push_back(std::move(newSlot));
            return nullptr;
        }
        AVFrame* hwFrame = newSlot.frame;
        av_frame_unref(hwFrame);
        hwFrame->format = AV_PIX_FMT_QSV;
        if (av_hwframe_get_buffer(m_hw_frames_ctx, hwFrame, 0) < 0) {
            LOGE(TAG, "PrepareFrame: failed to get QSV buffer");
            m_nv12Pool.push_back(std::move(newSlot));
            return nullptr;
        }
        hwFrame->pts = pts;
        mfxFrameSurface1* surface = reinterpret_cast<mfxFrameSurface1*>(hwFrame->data[3]);
        mfxHDLPair* pair = surface && surface->Data.MemId ? reinterpret_cast<mfxHDLPair*>(surface->Data.MemId) : nullptr;
        ID3D11Texture2D* qsvTex = pair ? reinterpret_cast<ID3D11Texture2D*>(pair->first) : nullptr;
        if (qsvTex) {
            UINT subresource = (pair->second == reinterpret_cast<void*>(0xFFFFFFFF))
                               ? 0 : static_cast<UINT>(reinterpret_cast<uintptr_t>(pair->second));
            m_d3dContext->CopySubresourceRegion(qsvTex, subresource, 0, 0, 0, newSlot.nv12Texture, 0, nullptr);
            m_d3dContext->End(newSlot.copyQuery);
            m_pendingRelease.push_back(std::move(newSlot));
            return hwFrame;
        } else {
            LOGE(TAG, "PrepareFrame: QSV texture is null");
            m_nv12Pool.push_back(std::move(newSlot));
        }
    }
    return nullptr;
}

void AvcodecGpuPipeline::DrainPendingRelease() {
    while (!m_pendingRelease.empty()) {
        NV12TextureSet& slot = m_pendingRelease.front();
        if (!slot.IsCopyComplete(m_d3dContext)) break;
        bool resolutionMatches = true;
        if (slot.nv12Texture) {
            D3D11_TEXTURE2D_DESC desc;
            slot.nv12Texture->GetDesc(&desc);
            resolutionMatches = (desc.Width == static_cast<UINT>(m_width) && desc.Height == static_cast<UINT>(m_height));
        }
        if (resolutionMatches)
            m_nv12Pool.push_back(std::move(slot));
        else {
            LOGI(TAG, "DrainPendingRelease: slot resolution mismatch, cleaning old slot");
            CleanupSlot(slot);
        }
        m_pendingRelease.pop_front();
    }
}

void AvcodecGpuPipeline::FinishFrame(AVFrame* frame) {
    // slot ownership transferred to m_pendingRelease in PrepareFrame — nothing to do here
}