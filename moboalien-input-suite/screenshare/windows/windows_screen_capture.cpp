#include "screenshare/windows/windows_screen_capture.h"
#include "utils.h"
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <gdiplus.h>
using namespace Gdiplus;

#include <immintrin.h> // SSE2/AVX2 SIMD intrinsics
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

static const char* TAG = "WindowsScreenCapture";
static const int ACQUIRE_FRAME_WAIT_TIMEOUT = 100;
static const int GDI_FRAME_REFRESH_TIME = 16;

#if defined(__AVX2__)
static inline int FindRunEnd(const int* table, int start, int end, int key) {
    __m256i vkey = _mm256_set1_epi32(key);
    while (start + 8 <= end) {
        int mask = _mm256_movemask_epi8(_mm256_cmpeq_epi32(
            _mm256_loadu_si256((const __m256i*)(table + start)), vkey));
        if (mask != (int)0xFFFFFFFF) return start + _tzcnt_u32(~(unsigned)mask) / 4;
        start += 8;
    }
    while (start < end && table[start] == key) ++start;
    return start;
}
static inline void FillRun(uint32_t* dst, uint32_t px, int len) {
    __m256i vpx = _mm256_set1_epi32((int)px);
    int i = 0;
    for (; i + 8 <= len; i += 8) _mm256_storeu_si256((__m256i*)(dst + i), vpx);
    for (; i < len; ++i) dst[i] = px;
}
#elif defined(__SSE2__)
static inline int FindRunEnd(const int* table, int start, int end, int key) {
    __m128i vkey = _mm_set1_epi32(key);
    while (start + 4 <= end) {
        int mask = _mm_movemask_epi8(_mm_cmpeq_epi32(
            _mm_loadu_si128((const __m128i*)(table + start)), vkey));
        unsigned long bit;
        if (mask != (int)0xFFFF) { _BitScanForward(&bit, ~(unsigned)mask); return start + (int)bit / 4; }
        start += 4;
    }
    while (start < end && table[start] == key) ++start;
    return start;
}
static inline void FillRun(uint32_t* dst, uint32_t px, int len) {
    __m128i vpx = _mm_set1_epi32((int)px);
    int i = 0;
    for (; i + 4 <= len; i += 4) _mm_storeu_si128((__m128i*)(dst + i), vpx);
    for (; i < len; ++i) dst[i] = px;
}
#else
static inline int FindRunEnd(const int* table, int start, int end, int key) {
    while (start < end && table[start] == key) ++start;
    return start;
}
static inline void FillRun(uint32_t* dst, uint32_t px, int len) {
    for (int i = 0; i < len; ++i) dst[i] = px;
}
#endif
// --- Constructor / Destructor ---

WindowsScreenCapture::WindowsScreenCapture(Platform* platform)
    : m_screenDC(NULL),
      m_d3dDevice(NULL),
      m_d3dContext(NULL),
      m_duplication(NULL),
      m_memoryDC(NULL),
      m_reusableBitmap(NULL),
      m_bitmapData(NULL),
      m_cursorBitmap(NULL),
      m_cursorBitmapData(NULL),
      m_cursorDC(NULL),
      m_cursorWidth(0),
      m_cursorHeight(0),
      m_scalerLastLogTime(std::chrono::steady_clock::now()),
      m_platform(platform)
{
}

WindowsScreenCapture::~WindowsScreenCapture()
{
    StopCaptureThread();
    if (m_config.method == CAPTURE_GDI) {
        if (m_cursorBitmap) { DeleteObject(m_cursorBitmap); m_cursorBitmap = NULL; }
        if (m_cursorDC) { DeleteDC(m_cursorDC); m_cursorDC = NULL; }
        if (m_reusableBitmap) { DeleteObject(m_reusableBitmap); m_reusableBitmap = NULL; }
        if (m_memoryDC) { DeleteDC(m_memoryDC); m_memoryDC = NULL; }
        if (m_screenDC) { ReleaseDC(NULL, m_screenDC); m_screenDC = NULL; }
        if (m_sharedTexture) { m_sharedTexture->Release(); m_sharedTexture = nullptr; }
    } else {
        CleanupD3D();
    }
}

// --- Public Control Methods ---

bool WindowsScreenCapture::Initialize(const CaptureConfig& config)
{
    m_config = config;
    if (m_config.method == CAPTURE_DESKTOP_DUPLICATION) {
        if (!InitializeD3D()) return false;
        if (!InitializeDuplication()) {
            LOGW(TAG, "Desktop Duplication failed to initialize, falling back to GDI");
            m_config.method = CAPTURE_GDI;
            CleanupD3D();
        }
    }

    if (m_config.method == CAPTURE_GDI) {
        // GDI path now also uses CreateDIBSection for direct memory access.
        m_screenDC = GetDC(NULL);
        m_memoryDC = CreateCompatibleDC(m_screenDC);
    } else { // Desktop Duplication
        m_memoryDC = CreateCompatibleDC(NULL);
    }

    // Unify bitmap creation for both GDI and DD to use DIB sections.
    // This guarantees a direct pointer to the pixel data (m_bitmapData).
    if (m_reusableBitmap == NULL) {
        int targetWidth, targetHeight;
        m_platform->GetScreenDimensions(targetWidth, targetHeight);
        targetWidth  = (int)(targetWidth  * m_config.scale) & ~1;
        targetHeight = (int)(targetHeight * m_config.scale) & ~1;
        EnsureReusableBitmap(targetWidth, targetHeight);
    }

    if (m_config.scaleMethod == SCALE_METHOD_AUTO && m_config.scale != 1.0f) {
        // Probe VPE once to resolve AUTO to a concrete method
        if (InitVPE()) {
            m_config.scaleMethod = SCALE_METHOD_VPE;
            LOGI(TAG, "SCALE_METHOD_AUTO resolved to VPE");
        } else if (InitGPUScaler()) {
            m_config.scaleMethod = SCALE_METHOD_SHADER;
            LOGI(TAG, "SCALE_METHOD_AUTO resolved to SHADER");
        } else {
            m_config.scaleMethod = SCALE_METHOD_CPU;
            LOGI(TAG, "SCALE_METHOD_AUTO resolved to CPU (fallback)");
        }
    }

    static const char* captureMethods[] = { "DESKTOP_DUPLICATION", "GDI" };
    {
        std::stringstream ss;
        ss << "Config: method=" << captureMethods[m_config.method]
           << " scale=" << m_config.scale
           << " scaleMethod=" << m_config.scaleMethod
           << " minFps=" << m_config.minFps;
        LOGI(TAG, ss.str());
    }

#if defined(__AVX2__)
    LOGI(TAG, "SIMD: AVX2 available");
#elif defined(__SSE2__)
    LOGI(TAG, "SIMD: SSE2 available (no AVX2)");
#else
    LOGI(TAG, "SIMD: none available");
#endif

    return true;
}

// --- Initialization Helpers ---

bool WindowsScreenCapture::InitializeD3D()
{
    if (m_d3dDevice) return true;

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        NULL, 0, D3D11_SDK_VERSION,
        &m_d3dDevice, &featureLevel, &m_d3dContext
    );
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "Failed to create D3D11 device. Error: 0x" << std::hex << hr << std::dec;
        LOGE(TAG, ss.str());
        return false;
    }

    // Set GPU thread priority to high for this device
    IDXGIDevice2* pDXGIDevice = nullptr;
    HRESULT priority_hr = m_d3dDevice->QueryInterface(__uuidof(IDXGIDevice2), (void**)&pDXGIDevice);
    if (SUCCEEDED(priority_hr) && pDXGIDevice) {
        // Priority levels range from -7 (lowest) to 7 (highest).
        priority_hr = pDXGIDevice->SetGPUThreadPriority(7);
        if (SUCCEEDED(priority_hr)) {
            LOGI(TAG, "Successfully set GPU thread priority to high (7).");
        } else {
            LOGW(TAG, "Failed to set GPU thread priority, hr=0x" + std::to_string(priority_hr));
        }
        pDXGIDevice->Release();
        pDXGIDevice = nullptr;
    } else {
        LOGW(TAG, "Failed to query IDXGIDevice2 interface, cannot set GPU thread priority.");
    }

    return true;
}

bool WindowsScreenCapture::InitializeDuplication()
{
    if (m_duplication) return true;
    if (!m_d3dDevice) return false;

    IDXGIDevice* dxgiDevice = NULL;
    IDXGIAdapter* dxgiAdapter = NULL;
    IDXGIOutput* dxgiOutput = NULL;
    IDXGIOutput1* dxgiOutput1 = NULL;

    HRESULT hr = m_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    if (FAILED(hr)) return false;

    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    dxgiDevice->Release();

    if (FAILED(hr)) return false;

    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
    dxgiAdapter->Release();
    if (FAILED(hr)) return false;

    hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1);
    dxgiOutput->Release();
    if (FAILED(hr)) return false;

    hr = dxgiOutput1->DuplicateOutput(m_d3dDevice, &m_duplication);
    dxgiOutput1->Release();

    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "Failed to create duplicate output. Error: 0x" << std::hex << hr << std::dec;
        LOGE(TAG, ss.str());
    }

    return SUCCEEDED(hr);
}

void WindowsScreenCapture::EnsureReusableBitmap(int width, int height)
{
    if (m_reusableBitmap) {
        BITMAP bmp;
        GetObject(m_reusableBitmap, sizeof(BITMAP), &bmp);
        if (bmp.bmWidth == width && bmp.bmHeight == height) {
            return;
        }
        DeleteObject(m_reusableBitmap);
        m_reusableBitmap = NULL;
    }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    m_reusableBitmap = CreateDIBSection(m_memoryDC, &bmi, DIB_RGB_COLORS, &m_bitmapData, NULL, 0);
    if (m_reusableBitmap) SelectObject(m_memoryDC, m_reusableBitmap);
}

// --- Capture Method ---

ImageUtils::RawImageFrame WindowsScreenCapture::CaptureFrame(bool withCursor, ImageUtils::PixelFormat format)
{
    m_lastCaptureFrameTime.store(GetTickCount64(), std::memory_order_relaxed);
    if (format != m_config.outputFormat) {
        LOGI(TAG, "CaptureFrame: format changed, rebuilding pipeline");
        m_config.outputFormat = format;
        m_scaledVpeLogged = false; // force re-log on next frame
        m_vpFormat = DXGI_FORMAT_UNKNOWN; // reset VPE format so it will be re-probed
    }
    if (!m_captureRunning) StartCaptureThread();
    std::lock_guard<std::mutex> lock(m_frameMutex);
    int readIdx = 1 - m_writeIdx;
    FrameBuffer& fb = m_frameBuffers[readIdx];
    if (!fb.data || fb.data->empty() || fb.format != format) {
        if (!fb.data || fb.data->empty())
            LOGW(TAG, "CaptureFrame: no frame ready yet (buffer empty)");
        else
            LOGW(TAG, "CaptureFrame: format mismatch, buffer has format=" + std::to_string((int)fb.format) + " requested=" + std::to_string((int)format));
        return {};
    }
    ImageUtils::RawImageFrame frame;
    frame.width     = fb.width;
    frame.height    = fb.height;
    frame.stride    = fb.stride;
    frame.format    = fb.format;
    frame.sharedData = fb.data;
    frame.data      = frame.sharedData->data();
    frame.timestampMs = fb.timestampMs;
    return frame;
}

void* WindowsScreenCapture::GetRenderDevice()
{
    return m_d3dDevice;
}

float WindowsScreenCapture::GetScale() const
{
    return m_config.scale;
}

uint64_t WindowsScreenCapture::GetLastCaptureTimestamp() {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    return m_frameBuffers[1 - m_writeIdx].timestampMs;
}

void WindowsScreenCapture::StartCaptureThread()
{
    if (m_captureRunning) return;
    CleanupD3D(); // Ensure any existing D3D resources are released before starting the thread
    Initialize(m_config); // Re-initialize to set up D3D and duplication in the capture thread
    m_captureRunning = true;
    if (!m_platform->CreateThread(&m_captureThread, CaptureThreadProc, this)) {
        m_captureRunning = false;
        LOGE(TAG, "StartCaptureThread: Platform::CreateThread failed");
    } else {
        LOGI(TAG, "StartCaptureThread: capture thread started");
    }
}

void WindowsScreenCapture::StopCaptureThread()
{
    if (!m_captureRunning) return;
    if (m_captureThread) {
        m_platform->JoinThread(m_captureThread);
        m_captureThread = nullptr;
        m_captureRunning = false;
        LOGI(TAG, "StopCaptureThread: capture thread stopped");
    }
}

void* WindowsScreenCapture::CaptureThreadProc(void* param)
{
    static_cast<WindowsScreenCapture*>(param)->CaptureThreadLoop();
    return nullptr;
}

void WindowsScreenCapture::CaptureThreadLoop()
{
    if (!m_platform->EnableMMCSSForCurrentThread()) {
        m_platform->SetCurrentThreadHighPriority();
    }
    while (m_captureRunning) {
        if (IsCaptureIdle()) {
            LOGI(TAG, "CaptureThreadLoop: no CaptureFrame call for 10s, stopping");
            m_captureRunning = false;
            break;
        }
        auto t0 = std::chrono::steady_clock::now();
        uint64_t captureTime = m_platform->GetTickCountMs();

        // Capture into the write-side reusable bitmap
        bool ok = false;
        if (m_config.method == CAPTURE_GDI) {
            int fullW, fullH;
            m_platform->GetScreenDimensions(fullW, fullH);
            int dstW = (int)(fullW * m_config.scale) & ~1;
            int dstH = (int)(fullH * m_config.scale) & ~1;
            EnsureReusableBitmap(dstW, dstH);
            if (m_reusableBitmap) {
                if (m_config.scale == 1.0f)
                    BitBlt(m_memoryDC, 0, 0, dstW, dstH, m_screenDC, 0, 0, SRCCOPY);
                else {
                    SetStretchBltMode(m_memoryDC, COLORONCOLOR);
                    StretchBlt(m_memoryDC, 0, 0, dstW, dstH, m_screenDC, 0, 0, fullW, fullH, SRCCOPY);
                }
                ok = true;
            } else {
                LOGE(TAG, "CaptureThreadLoop: EnsureReusableBitmap failed");
            }
        } else {
            // For DD path, pre-size wb.data and write directly into it
            int dstW, dstH;
            m_platform->GetScreenDimensions(dstW, dstH);
            dstW = (int)(dstW * m_config.scale) & ~1;
            dstH = (int)(dstH * m_config.scale) & ~1;
            bool isNV12 = (m_config.outputFormat == ImageUtils::PixelFormat::NV12);
            size_t bytes = isNV12 ? (size_t)dstW * dstH * 3 / 2 : (size_t)dstW * 4 * dstH;
            FrameBuffer& wb = m_frameBuffers[m_writeIdx];
            if (!wb.data || wb.data.use_count() > 1) {
                wb.data = std::make_shared<std::vector<unsigned char>>();
            }
            wb.data->resize(bytes);
            ok = CaptureAndConvertToReusableBitmap(wb.data->data(), dstW, dstH);
            if (ok) {
                wb.width  = dstW;
                wb.height = dstH;
                wb.stride = isNV12 ? dstW : dstW * 4;
                wb.format = m_config.outputFormat;
                wb.timestampMs = captureTime;
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_writeIdx = 1 - m_writeIdx;
            } else {
                LOGD(TAG, "CaptureThreadLoop: CaptureAndConvertToReusableBitmap returned false");
            }
        }

        if (ok && m_config.method == CAPTURE_GDI && m_bitmapData && m_reusableBitmap) {
            BITMAP bmp;
            GetObject(m_reusableBitmap, sizeof(BITMAP), &bmp);
            size_t bytes = (size_t)bmp.bmWidthBytes * bmp.bmHeight;

            std::lock_guard<std::mutex> lock(m_frameMutex);
            FrameBuffer& wb = m_frameBuffers[m_writeIdx];
            wb.width  = bmp.bmWidth;
            wb.height = bmp.bmHeight;
            wb.stride = bmp.bmWidthBytes;
            wb.timestampMs = captureTime;
            if (!wb.data || wb.data.use_count() > 1) {
                wb.data = std::make_shared<std::vector<unsigned char>>();
            }
            wb.data->resize(bytes);
            memcpy(wb.data->data(), m_bitmapData, bytes);
            m_writeIdx = 1 - m_writeIdx;
        }

        if (m_config.method == CAPTURE_GDI) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            int sleepMs = GDI_FRAME_REFRESH_TIME - (int)elapsed;
            if (sleepMs > 0) m_platform->Sleep(sleepMs);
        }
    }
}

ID3D11Texture2D* WindowsScreenCapture::AcquireNextDesktopFrame()
{
    if (!m_duplication) {
        if (!InitializeDuplication()) return nullptr;
    }

    IDXGIResource* desktopResource = NULL;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    HRESULT hr = m_duplication->AcquireNextFrame(ACQUIRE_FRAME_WAIT_TIMEOUT, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        if (!m_timeoutFlushDone && m_sharedTexture) {
            m_timeoutFlushDone = true;
            return m_sharedTexture; // one flush pass to drain staging pipeline
        }
        return nullptr;
    }
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            LOGW(TAG, "Desktop duplication lost, reinitializing...");
        } else {
            std::stringstream ss;
            ss << "AcquireNextFrame failed, hr=0x" << std::hex << hr;
            LOGE(TAG, ss.str());
        }
        m_duplication->Release();
        m_duplication = NULL;
        InitializeDuplication();
        return nullptr;
    }

    ID3D11Texture2D* acquiredTexture = NULL;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&acquiredTexture);
    desktopResource->Release();
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "QueryInterface for ID3D11Texture2D failed, hr=0x" << std::hex << hr;
        LOGE(TAG, ss.str());
        m_duplication->ReleaseFrame();
        return nullptr;
    }

    // Copy to m_sharedTexture
    D3D11_TEXTURE2D_DESC desc;
    acquiredTexture->GetDesc(&desc);
    if (m_sharedTexture) {
        D3D11_TEXTURE2D_DESC currentDesc;
        m_sharedTexture->GetDesc(&currentDesc);
        if (currentDesc.Width != desc.Width || currentDesc.Height != desc.Height) {
            m_sharedTexture->Release();
            m_sharedTexture = nullptr;
        }
    }

    if (!m_sharedTexture) {
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = 0;
        desc.CPUAccessFlags = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        HRESULT texHr = m_d3dDevice->CreateTexture2D(&desc, NULL, &m_sharedTexture);
        if (FAILED(texHr)) {
            std::stringstream ss;
            ss << "Failed to create shared texture, hr=0x" << std::hex << texHr << " bindFlags=0x" << desc.BindFlags;
            LOGE(TAG, ss.str());
            acquiredTexture->Release();
            m_duplication->ReleaseFrame();
            return nullptr;
        }
        {
            std::stringstream ss;
            ss << "Shared texture created: format=" << desc.Format << " size=" << desc.Width << "x" << desc.Height << " bindFlags=0x" << std::hex << desc.BindFlags;
            LOGI(TAG, ss.str());
        }
    }
    m_d3dContext->CopyResource(m_sharedTexture, acquiredTexture);
    acquiredTexture->Release();
    m_duplication->ReleaseFrame();
    m_timeoutFlushDone = false; // real frame arrived, allow one flush on next timeout
    return m_sharedTexture;
}



ID3D11Texture2D* WindowsScreenCapture::AcquireNextDesktopFrameForCPU()
{
    if (!m_duplication) {
        if (!InitializeDuplication()) return nullptr;
    }

    IDXGIResource* desktopResource = NULL;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    HRESULT hr = m_duplication->AcquireNextFrame(ACQUIRE_FRAME_WAIT_TIMEOUT, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        if (!m_timeoutFlushDoneCPU && !m_sharedStagingTextures.empty()) {
            m_timeoutFlushDoneCPU = true;
            int n    = (int)m_sharedStagingTextures.size();
            int prev = (m_sharedStagingFrameIndex + n - 1) % n;
            return m_sharedStagingTextures[prev];
        }
        return nullptr;
    }
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            LOGW(TAG, "AcquireNextDesktopFrameForCPU: desktop duplication lost, reinitializing...");
        } else {
            std::stringstream ss;
            ss << "AcquireNextDesktopFrameForCPU: AcquireNextFrame failed, hr=0x" << std::hex << hr;
            LOGE(TAG, ss.str());
        }
        m_duplication->Release();
        m_duplication = NULL;
        InitializeDuplication();
        return nullptr;
    }

    ID3D11Texture2D* acquiredTexture = NULL;
    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&acquiredTexture);
    desktopResource->Release();
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "AcquireNextDesktopFrameForCPU: QueryInterface for ID3D11Texture2D failed, hr=0x" << std::hex << hr;
        LOGE(TAG, ss.str());
        m_duplication->ReleaseFrame();
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC desc;
    acquiredTexture->GetDesc(&desc);
    if (m_sharedStagingTexture) {
        D3D11_TEXTURE2D_DESC currentDesc;
        m_sharedStagingTexture->GetDesc(&currentDesc);
        if (currentDesc.Width != desc.Width || currentDesc.Height != desc.Height) {
            for (auto* t : m_sharedStagingTextures) { if (t) t->Release(); }
            m_sharedStagingTextures.clear();
            m_sharedStagingTexture = nullptr;
            m_sharedStagingFrameIndex = 0;
        }
    }

    if (!m_sharedStagingTexture) {
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        int count = std::max(2, m_config.stagingFrameCount);
        m_sharedStagingTextures.resize(count, nullptr);
        for (int i = 0; i < count; ++i) {
            HRESULT texHr = m_d3dDevice->CreateTexture2D(&desc, NULL, &m_sharedStagingTextures[i]);
            if (FAILED(texHr)) {
                std::stringstream ss;
                ss << "AcquireNextDesktopFrameForCPU: failed to create staging texture[" << i << "], hr=0x" << std::hex << texHr;
                LOGE(TAG, ss.str());
                acquiredTexture->Release();
                m_duplication->ReleaseFrame();
                return nullptr;
            }
        }
        m_sharedStagingTexture = m_sharedStagingTextures[0];
        m_sharedStagingFrameIndex = 0;
        {
            std::stringstream ss;
            ss << "AcquireNextDesktopFrameForCPU: " << count << " staging textures created: format=" << desc.Format << " size=" << desc.Width << "x" << desc.Height;
            LOGI(TAG, ss.str());
        }
    }

    int n    = (int)m_sharedStagingTextures.size();
    int cur  = m_sharedStagingFrameIndex % n;
    int prev = (m_sharedStagingFrameIndex + n - 1) % n;
    m_d3dContext->CopyResource(m_sharedStagingTextures[cur], acquiredTexture);
    m_sharedStagingFrameIndex++;
    acquiredTexture->Release();
    m_duplication->ReleaseFrame();
    m_timeoutFlushDoneCPU = false;
    return (m_sharedStagingFrameIndex <= 1) ? m_sharedStagingTextures[cur] : m_sharedStagingTextures[prev];
}


void* WindowsScreenCapture::GetCaptureTexture()
{
    if (m_captureRunning) {
        LOGW(TAG, "GetCaptureTexture called while capture thread is running, stopping it");
        StopCaptureThread();
    }
    return CaptureD3D11Texture();
}

ID3D11Texture2D* WindowsScreenCapture::CaptureD3D11Texture()
{
    if (m_config.method != CAPTURE_DESKTOP_DUPLICATION) return nullptr;
    return AcquireNextDesktopFrame();
}

void WindowsScreenCapture::SetCustomDevice(void* device) {
    if (device && device != m_d3dDevice) {
        StopCaptureThread();
        if (m_duplication) { m_duplication->Release(); m_duplication = NULL; }
        if (m_sharedTexture) { m_sharedTexture->Release(); m_sharedTexture = nullptr; }
        if (m_d3dContext) { m_d3dContext->Release(); m_d3dContext = NULL; }
        if (m_d3dDevice) { m_d3dDevice->Release(); m_d3dDevice = NULL; }
        
        m_d3dDevice = static_cast<ID3D11Device*>(device);
        m_d3dDevice->AddRef();
        m_d3dDevice->GetImmediateContext(&m_d3dContext);
        Initialize(m_config); // Re-initialize to set up duplication with the new device
    }
}

CursorFrame WindowsScreenCapture::GetCursorFrame() {
    CursorFrame cursorFrame;
    cursorFrame.isMonochrome = false;
    cursorFrame.isVisible = false;

    CURSORINFO cursorInfo = { 0 };
    cursorInfo.cbSize = sizeof(CURSORINFO);

    if (!GetCursorInfo(&cursorInfo) || !(cursorInfo.flags & CURSOR_SHOWING))
        return cursorFrame;

    ICONINFO iconInfo;
    if (!GetIconInfo(cursorInfo.hCursor, &iconInfo))
        return cursorFrame;

    BITMAP bmp = { 0 };
    if (iconInfo.hbmColor != NULL)
        GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmp);
    else
        GetObject(iconInfo.hbmMask, sizeof(BITMAP), &bmp);

    int width = bmp.bmWidth;
    int height = iconInfo.hbmColor ? bmp.bmHeight : bmp.bmHeight / 2;

    if (!m_cursorDC)
        m_cursorDC = CreateCompatibleDC(NULL);

    if (!m_cursorBitmap || m_cursorWidth != width || m_cursorHeight != height) {
        if (m_cursorBitmap) DeleteObject(m_cursorBitmap);
        m_cursorWidth = width;
        m_cursorHeight = height;

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        m_cursorBitmap = CreateDIBSection(m_cursorDC, &bmi, DIB_RGB_COLORS, &m_cursorBitmapData, NULL, 0);
        SelectObject(m_cursorDC, m_cursorBitmap);
    }

    bool isMonochrome = (iconInfo.hbmColor == NULL);
    cursorFrame.isMonochrome = isMonochrome;

    // Cache for monochrome cursors. Key is monochromeCursorHash of mask bits, value is the rendered 32bpp image data.
    static std::unordered_map<uint32_t, std::vector<unsigned char>> m_monochromeCache;
    if (isMonochrome) {
        // Read mask bits to compute monochromeCursorHash
        BITMAP maskBmp = {};
        GetObject(iconInfo.hbmMask, sizeof(BITMAP), &maskBmp);
        size_t maskSize = maskBmp.bmWidthBytes * maskBmp.bmHeight;
        std::vector<uint8_t> maskBits(maskSize);
        GetBitmapBits(iconInfo.hbmMask, static_cast<LONG>(maskSize), maskBits.data());

        uint32_t monochromeCursorHash = HashData(maskBits.data(), maskSize, 37);
        cursorFrame.monochromeCursorHash = monochromeCursorHash;
        auto it = m_monochromeCache.find(monochromeCursorHash);

        if (it != m_monochromeCache.end()) {
            // Cache hit: Copy the pre-rendered bitmap data.
            memcpy(m_cursorBitmapData, it->second.data(), it->second.size());
        } else {
            // Cache miss: Render a fresh image.
            memset(m_cursorBitmapData, 0, width * height * 4);

            uint32_t* pixels = static_cast<uint32_t*>(m_cursorBitmapData);
            uint32_t opaqueBlack = 0xFF000000;
            int stride = maskBmp.bmWidthBytes;
            const uint8_t* bits = maskBits.data();

            for (int y = 0; y < height; ++y) {
                const uint8_t* andRow = bits + y * stride;
                const uint8_t* xorRow = bits + (y + height) * stride;

                for (int x = 0; x < width; ++x) {
                    uint8_t mask = 0x80 >> (x % 8);
                    bool andBit = (andRow[x / 8] & mask) != 0;
                    bool xorBit = (xorRow[x / 8] & mask) != 0;

                    // Transparent if AND=1 (White) and XOR=0 (Black). Otherwise draw.
                    if (!(andBit && !xorBit)) {
                        pixels[y * width + x] = opaqueBlack;
                    }
                }
            }
            // Add the newly rendered image to the cache.
            const auto* data_ptr = static_cast<const unsigned char*>(m_cursorBitmapData);
            m_monochromeCache[monochromeCursorHash] = std::vector<unsigned char>(data_ptr, data_ptr + (width * height * 4));
        }
    } else {
        Graphics graphics(m_cursorDC);
        graphics.SetCompositingMode(CompositingModeSourceCopy);
        graphics.Clear(Color(0, 0, 0, 0));
        DrawIconEx(m_cursorDC, 0, 0, cursorInfo.hCursor, 0, 0, 0, NULL, DI_NORMAL);
    }

    cursorFrame.isVisible = true;
    cursorFrame.x = (int)(cursorInfo.ptScreenPos.x * m_config.scale) - iconInfo.xHotspot;
    cursorFrame.y = (int)(cursorInfo.ptScreenPos.y * m_config.scale) - iconInfo.yHotspot;
    cursorFrame.image.width = width;
    cursorFrame.image.height = height;
    cursorFrame.image.stride = width * 4;
    cursorFrame.image.format = ImageUtils::PixelFormat::BGRA32;
    cursorFrame.image.data = static_cast<const unsigned char*>(m_cursorBitmapData);

    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
    return cursorFrame;
}
// --- Cleanup Methods ---

void WindowsScreenCapture::CleanupD3D()
{
    if (m_reusableBitmap) { DeleteObject(m_reusableBitmap); m_reusableBitmap = NULL; }
    if (m_memoryDC) { DeleteDC(m_memoryDC); m_memoryDC = NULL; }
    for (auto* t : m_sharedStagingTextures) { if (t) t->Release(); }
    m_sharedStagingTextures.clear();
    m_sharedStagingTexture = nullptr;
    m_sharedStagingFrameIndex = 0;
    if (m_scaledRTV)     { m_scaledRTV->Release();     m_scaledRTV = nullptr; }
    if (m_scaledSRV)     { m_scaledSRV->Release();     m_scaledSRV = nullptr; }
    if (m_scaledTexture) { m_scaledTexture->Release(); m_scaledTexture = nullptr; }
    for (int i = 0; i < 2; ++i) {
        if (m_scaledStaging[i]) { m_scaledStaging[i]->Release(); m_scaledStaging[i] = nullptr; }
    }
    m_scaledW = m_scaledH = 0;
    m_scaledFrameIndex = 0;
    if (m_scaleVS)       { m_scaleVS->Release();       m_scaleVS = nullptr; }
    if (m_scalePS)       { m_scalePS->Release();       m_scalePS = nullptr; }
    if (m_linearSampler) { m_linearSampler->Release(); m_linearSampler = nullptr; }
    if (m_rasterizerState) { m_rasterizerState->Release(); m_rasterizerState = nullptr; }
    if (m_vpOutputView)  { m_vpOutputView->Release();  m_vpOutputView = nullptr; }
    if (m_vpOutputTex)   { m_vpOutputTex->Release();   m_vpOutputTex = nullptr; }
    for (int i = 0; i < 2; ++i) {
        if (m_vpStaging[i]) { m_vpStaging[i]->Release(); m_vpStaging[i] = nullptr; }
    }
    m_vpFrameIndex = 0;
    if (m_vpProcessor)   { m_vpProcessor->Release();   m_vpProcessor = nullptr; }
    if (m_vpEnum)        { m_vpEnum->Release();        m_vpEnum = nullptr; }
    if (m_videoContext)  { m_videoContext->Release();  m_videoContext = nullptr; }
    if (m_videoDevice)   { m_videoDevice->Release();   m_videoDevice = nullptr; }
    m_vpSrcW = m_vpSrcH = m_vpDstW = m_vpDstH = 0;
    m_vpFormat = DXGI_FORMAT_UNKNOWN;
    if (m_duplication) { m_duplication->Release(); m_duplication = NULL; }
    if (m_d3dContext) { m_d3dContext->Release(); m_d3dContext = NULL; }
    if (m_d3dDevice) { m_d3dDevice->Release(); m_d3dDevice = NULL; }
    if (m_sharedTexture) { m_sharedTexture->Release(); m_sharedTexture = nullptr; }
}

bool WindowsScreenCapture::InitGPUScaler()
{
    if (m_scaleVS) return true;

    // Fullscreen triangle VS — no vertex buffer needed
    static const char* vsCode = R"(
struct VS_OUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VS_OUT main(uint id : SV_VertexID) {
    VS_OUT o;
    o.uv  = float2((id & 1) ? 2.0 : 0.0, (id & 2) ? 2.0 : 0.0);
    o.pos = float4(o.uv.x * 2 - 1, 1 - o.uv.y * 2, 0, 1);
    return o;
})";  // 3-vertex fullscreen triangle

    static const char* psCode = R"(
Texture2D    tex : register(t0);
SamplerState smp : register(s0);
struct VS_OUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
float4 main(VS_OUT i) : SV_TARGET { return tex.Sample(smp, i.uv); })"; 

    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;
    if (FAILED(D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &blob, &err))) {
        if (err) { LOGE(TAG, (char*)err->GetBufferPointer()); err->Release(); }
        return false;
    }
    m_d3dDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_scaleVS);
    blob->Release();

    if (FAILED(D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &blob, &err))) {
        if (err) { LOGE(TAG, (char*)err->GetBufferPointer()); err->Release(); }
        return false;
    }
    m_d3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_scalePS);
    blob->Release();

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    m_d3dDevice->CreateSamplerState(&sd, &m_linearSampler);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    m_d3dDevice->CreateRasterizerState(&rd, &m_rasterizerState);

    return m_scaleVS && m_scalePS && m_linearSampler && m_rasterizerState;
}

ID3D11Texture2D* WindowsScreenCapture::ScaleTextureOnGPU(ID3D11Texture2D* src, int dstW, int dstH)
{
    if (!InitGPUScaler()) return nullptr;

    // Recreate render target if size changed
    if (m_scaledW != dstW || m_scaledH != dstH) {
        if (m_scaledRTV)     { m_scaledRTV->Release();     m_scaledRTV = nullptr; }
        if (m_scaledSRV)     { m_scaledSRV->Release();     m_scaledSRV = nullptr; }
        if (m_scaledTexture) { m_scaledTexture->Release(); m_scaledTexture = nullptr; }
        for (int i = 0; i < 2; ++i) { if (m_scaledStaging[i]) { m_scaledStaging[i]->Release(); m_scaledStaging[i] = nullptr; } }

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = dstW; td.Height = dstH; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(m_d3dDevice->CreateTexture2D(&td, nullptr, &m_scaledTexture))) return nullptr;
        m_d3dDevice->CreateRenderTargetView(m_scaledTexture, nullptr, &m_scaledRTV);

        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (int i = 0; i < 2; ++i)
            if (FAILED(m_d3dDevice->CreateTexture2D(&td, nullptr, &m_scaledStaging[i]))) return nullptr;

        m_scaledW = dstW; m_scaledH = dstH;
        m_scaledFrameIndex = 0;
    }

    // Create SRV for the source texture — cached, recreated only when source texture changes
    if (!m_scaledSRV) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(m_d3dDevice->CreateShaderResourceView(src, &srvDesc, &m_scaledSRV))) return nullptr;
    }

    // Draw fullscreen triangle to scale
    D3D11_VIEWPORT vp = { 0, 0, (float)dstW, (float)dstH, 0, 1 };
    m_d3dContext->RSSetViewports(1, &vp);
    m_d3dContext->RSSetState(m_rasterizerState);
    m_d3dContext->OMSetRenderTargets(1, &m_scaledRTV, nullptr);
    m_d3dContext->VSSetShader(m_scaleVS, nullptr, 0);
    m_d3dContext->PSSetShader(m_scalePS, nullptr, 0);
    m_d3dContext->PSSetShaderResources(0, 1, &m_scaledSRV);
    m_d3dContext->PSSetSamplers(0, 1, &m_linearSampler);
    m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_d3dContext->IASetInputLayout(nullptr);
    m_d3dContext->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    m_d3dContext->PSSetShaderResources(0, 1, &nullSRV);
    ID3D11RenderTargetView* nullRTV = nullptr;
    m_d3dContext->OMSetRenderTargets(1, &nullRTV, nullptr);

    int cur = m_scaledFrameIndex % 2;
    int prev = (m_scaledFrameIndex + 1) % 2;
    m_d3dContext->CopyResource(m_scaledStaging[cur], m_scaledTexture);
    m_scaledFrameIndex++;
    return (m_scaledFrameIndex <= 1) ? m_scaledStaging[cur] : m_scaledStaging[prev];
}

bool WindowsScreenCapture::InitVPE()
{
    if (m_videoDevice) return true;

    HRESULT hr = m_d3dDevice->QueryInterface(__uuidof(ID3D11VideoDevice), (void**)&m_videoDevice);
    if (FAILED(hr)) {
        std::stringstream ss; ss << "InitVPE: QueryInterface(ID3D11VideoDevice) failed, hr=0x" << std::hex << hr;
        LOGE(TAG, ss.str()); return false;
    }

    hr = m_d3dContext->QueryInterface(__uuidof(ID3D11VideoContext), (void**)&m_videoContext);
    if (FAILED(hr)) {
        std::stringstream ss; ss << "InitVPE: QueryInterface(ID3D11VideoContext) failed, hr=0x" << std::hex << hr;
        LOGE(TAG, ss.str()); m_videoDevice->Release(); m_videoDevice = nullptr; return false;
    }

    LOGI(TAG, "InitVPE: ID3D11VideoDevice and ID3D11VideoContext acquired successfully");
    return true;
}

ID3D11Texture2D* WindowsScreenCapture::ScaleTextureVPE(ID3D11Texture2D* src, int dstW, int dstH, DXGI_FORMAT format)
{
    if (!InitVPE()) return nullptr;

    D3D11_TEXTURE2D_DESC srcDesc;
    src->GetDesc(&srcDesc);
    int srcW = (int)srcDesc.Width, srcH = (int)srcDesc.Height;

    if (srcW != m_vpSrcW || srcH != m_vpSrcH || dstW != m_vpDstW || dstH != m_vpDstH || format != m_vpFormat) {
        if (m_vpOutputView)  { m_vpOutputView->Release();  m_vpOutputView = nullptr; }
        if (m_vpOutputTex)   { m_vpOutputTex->Release();   m_vpOutputTex = nullptr; }
        for (int i = 0; i < 2; ++i) { if (m_vpStaging[i]) { m_vpStaging[i]->Release(); m_vpStaging[i] = nullptr; } }
        if (m_vpProcessor)   { m_vpProcessor->Release();   m_vpProcessor = nullptr; }
        if (m_vpEnum)        { m_vpEnum->Release();        m_vpEnum = nullptr; }

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC vpDesc = {};
        vpDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        vpDesc.InputWidth  = srcW;  vpDesc.InputHeight  = srcH;
        vpDesc.OutputWidth = dstW;  vpDesc.OutputHeight = dstH;
        vpDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        HRESULT hr = m_videoDevice->CreateVideoProcessorEnumerator(&vpDesc, &m_vpEnum);
        if (FAILED(hr)) {
            std::stringstream ss; ss << "ScaleTextureVPE: CreateVideoProcessorEnumerator failed, hr=0x" << std::hex << hr;
            LOGE(TAG, ss.str()); return nullptr;
        }

        hr = m_videoDevice->CreateVideoProcessor(m_vpEnum, 0, &m_vpProcessor);
        if (FAILED(hr)) {
            std::stringstream ss; ss << "ScaleTextureVPE: CreateVideoProcessor failed, hr=0x" << std::hex << hr;
            LOGE(TAG, ss.str()); return nullptr;
        }

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = dstW; td.Height = dstH;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = format;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        hr = m_d3dDevice->CreateTexture2D(&td, nullptr, &m_vpOutputTex);
        if (FAILED(hr)) {
            std::stringstream ss; ss << "ScaleTextureVPE: CreateTexture2D(output, format=0x" << std::hex << format << ") failed, hr=0x" << hr;
            LOGE(TAG, ss.str()); return nullptr;
        }

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovDesc = {};
        ovDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        ovDesc.Texture2D.MipSlice = 0;
        hr = m_videoDevice->CreateVideoProcessorOutputView(m_vpOutputTex, m_vpEnum, &ovDesc, &m_vpOutputView);
        if (FAILED(hr)) {
            std::stringstream ss; ss << "ScaleTextureVPE: CreateVideoProcessorOutputView failed, hr=0x" << std::hex << hr;
            LOGE(TAG, ss.str()); return nullptr;
        }

        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (int i = 0; i < 2; ++i) {
            hr = m_d3dDevice->CreateTexture2D(&td, nullptr, &m_vpStaging[i]);
            if (FAILED(hr)) {
                std::stringstream ss; ss << "ScaleTextureVPE: CreateTexture2D(staging[" << i << "]) failed, hr=0x" << std::hex << hr;
                LOGE(TAG, ss.str()); return nullptr;
            }
        }

        LOGI(TAG, "ScaleTextureVPE: pipeline created (format=" + std::to_string(format) + ")");
        m_vpSrcW = srcW; m_vpSrcH = srcH;
        m_vpDstW = dstW; m_vpDstH = dstH;
        m_vpFormat = format;
        m_vpFrameIndex = 0;
    }

    ComPtr<ID3D11VideoProcessorInputView> frameInputView;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivDesc = {};
    ivDesc.FourCC = 0;
    ivDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    ivDesc.Texture2D.MipSlice = 0;
    if (FAILED(m_videoDevice->CreateVideoProcessorInputView(src, m_vpEnum, &ivDesc, &frameInputView))) {
        D3D11_TEXTURE2D_DESC d; src->GetDesc(&d);
        std::stringstream ss; ss << "ScaleTextureVPE: CreateVideoProcessorInputView failed, src bindFlags=0x" << std::hex << d.BindFlags;
        LOGE(TAG, ss.str()); return nullptr;
    }

    RECT srcRect = { 0, 0, srcW, srcH };
    RECT dstRect = { 0, 0, dstW, dstH };
    m_videoContext->VideoProcessorSetStreamSourceRect(m_vpProcessor, 0, TRUE, &srcRect);
    m_videoContext->VideoProcessorSetStreamDestRect(m_vpProcessor, 0, TRUE, &dstRect);
    m_videoContext->VideoProcessorSetOutputTargetRect(m_vpProcessor, TRUE, &dstRect);

    D3D11_VIDEO_PROCESSOR_STREAM stream = {};
    stream.Enable = TRUE;
    stream.pInputSurface = frameInputView.Get();
    if (FAILED(m_videoContext->VideoProcessorBlt(m_vpProcessor, m_vpOutputView, 0, 1, &stream))) {
        LOGE(TAG, "ScaleTextureVPE: VideoProcessorBlt failed"); return nullptr;
    }

    int cur  = m_vpFrameIndex % 2;
    int prev = (m_vpFrameIndex + 1) % 2;
    m_d3dContext->CopyResource(m_vpStaging[cur], m_vpOutputTex);
    m_vpFrameIndex++;
    return (m_vpFrameIndex <= 1) ? m_vpStaging[cur] : m_vpStaging[prev];
}
// --- Utility Methods ---

bool WindowsScreenCapture::CopyStagingToReusableBitmap(ID3D11Texture2D* staging, int width, int height, unsigned char* dst)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_d3dContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        LOGE(TAG, "Failed to map staging texture");
        return false;
    }

    unsigned char* src = (unsigned char*)mapped.pData;
    if (mapped.RowPitch == (UINT)width * 4) {
        size_t totalBytes = (size_t)width * 4 * height;
        size_t i = 0;
        const __m128i* s = (const __m128i*)src;
              __m128i* d = (      __m128i*)dst;
        for (; i + 16 <= totalBytes; i += 16, ++s, ++d)
            _mm_stream_si128(d, _mm_loadu_si128(s));
        _mm_sfence();
        if (i < totalBytes) memcpy(dst + i, src + i, totalBytes - i);
    } else {
        for (int y = 0; y < height; ++y)
            memcpy(dst + y * width * 4, src + y * mapped.RowPitch, width * 4);
    }

    m_d3dContext->Unmap(staging, 0);
    return true;
}

void WindowsScreenCapture::SaveBitmapToFile(HBITMAP hBitmap, const char* filename)
{
    BITMAP bmp;
    GetObject(hBitmap, sizeof(BITMAP), &bmp);

    BITMAPFILEHEADER bfh = {0};
    BITMAPINFOHEADER bih = {0};

    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = bmp.bmWidth;
    bih.biHeight = bmp.bmHeight;
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;

    int dataSize = ((bmp.bmWidth * 3 + 3) & ~3) * bmp.bmHeight;

    bfh.bfType = 0x4D42; // 'BM'
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dataSize;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    HDC hdc = GetDC(NULL);
    unsigned char* data = new unsigned char[dataSize];
    GetDIBits(hdc, hBitmap, 0, bmp.bmHeight, data, (BITMAPINFO*)&bih, DIB_RGB_COLORS);

    FILE* file = fopen(filename, "wb");
    if (file) {
        fwrite(&bfh, sizeof(BITMAPFILEHEADER), 1, file);
        fwrite(&bih, sizeof(BITMAPINFOHEADER), 1, file);
        fwrite(data, dataSize, 1, file);
        fclose(file);
    }

    delete[] data;
    ReleaseDC(NULL, hdc);
}

ID3D11Texture2D* WindowsScreenCapture::ScaleWithVPE(ID3D11Texture2D* texture, int dstW, int dstH)
{
    DXGI_FORMAT fmt = (m_config.outputFormat == ImageUtils::PixelFormat::NV12)
        ? DXGI_FORMAT_NV12 : DXGI_FORMAT_B8G8R8A8_UNORM;
    if (!m_scaledVpeLogged) { LOGI(TAG, "Scaler: using VPE"); m_scaledVpeLogged = true; m_scalerMethodName = "VPE"; }
    return ScaleTextureVPE(texture, dstW, dstH, fmt);
}

ID3D11Texture2D* WindowsScreenCapture::ScaleWithShader(ID3D11Texture2D* texture, int dstW, int dstH)
{
    if (!m_scaledShaderLogged) { LOGI(TAG, "Scaler: using shader blit"); m_scaledShaderLogged = true; m_scalerMethodName = "Shader"; }
    return ScaleTextureOnGPU(texture, dstW, dstH);
}

// Returns a staging texture containing the scaled result, or nullptr on failure.
ID3D11Texture2D* WindowsScreenCapture::GetScaledStagingTexture(ID3D11Texture2D* texture, int dstW, int dstH)
{
    if (m_config.scaleMethod == SCALE_METHOD_CPU)
        return nullptr;

    auto t0 = std::chrono::steady_clock::now();

    ID3D11Texture2D* result =
        (m_config.scaleMethod == SCALE_METHOD_VPE)    ? ScaleWithVPE(texture, dstW, dstH) :
        (m_config.scaleMethod == SCALE_METHOD_SHADER) ? ScaleWithShader(texture, dstW, dstH) :
        nullptr;

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    m_scalerTotalUs += us;
    m_scalerFrameCount++;

    auto now = std::chrono::steady_clock::now();
    if (m_scalerFrameCount > 0 && std::chrono::duration_cast<std::chrono::seconds>(now - m_scalerLastLogTime).count() >= 10) {
        std::stringstream ss;
        ss << "Scaler stats [" << m_scalerMethodName << "]: avg=" << (m_scalerTotalUs / m_scalerFrameCount) << "us over " << m_scalerFrameCount << " frames";
        LOGI(TAG, ss.str());
        m_scalerTotalUs = 0;
        m_scalerMapTotalUs = 0;
        m_scalerFrameCount = 0;
        m_scalerLastLogTime = now;
    }

    return result;
}

// CPU nearest-neighbor scale: maps staging texture directly (no extra copy needed).
bool WindowsScreenCapture::ScaleOnCPU(ID3D11Texture2D* stagingTexture, int dstW, int dstH, unsigned char* dst)
{
    if (!m_scaledShaderLogged) { LOGI(TAG, "Scaler: using CPU"); m_scaledShaderLogged = true; m_scalerMethodName = "CPU"; }
    D3D11_TEXTURE2D_DESC desc;
    stagingTexture->GetDesc(&desc);

    auto tMap0 = std::chrono::steady_clock::now();
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_d3dContext->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped))) {
        LOGE(TAG, "ScaleOnCPU: failed to map staging texture");
        return false;
    }
    auto mapUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tMap0).count();
    if (mapUs > 5000)
        LOGW(TAG, "ScaleOnCPU: Map stalled for " + std::to_string(mapUs) + "us — GPU copy not yet done (first frame or resize)");

    if (!dst) { m_d3dContext->Unmap(stagingTexture, 0); return false; }

    auto t0 = std::chrono::steady_clock::now();
    const int srcW = (int)desc.Width, srcH = (int)desc.Height;
    const unsigned char* src = (const unsigned char*)mapped.pData;

    if (m_srcXTableWidth != dstW || m_srcScaleCached != m_config.scale) {
        m_srcXTable.resize(dstW);
        for (int x = 0; x < dstW; ++x)
            m_srcXTable[x] = std::min((int)(x / m_config.scale), srcW - 1);
        m_srcXTableWidth = dstW;
        m_srcScaleCached = m_config.scale;
    }

    for (int y = 0; y < dstH; ++y) {
        int iy = std::min((int)(y / m_config.scale), srcH - 1);
        const uint32_t* srcRow = (const uint32_t*)(src + iy * mapped.RowPitch);
        uint32_t* dstRow = (uint32_t*)(dst + y * dstW * 4);
        for (int x = 0; x < dstW; ++x)
            dstRow[x] = srcRow[m_srcXTable[x]];
    }
    m_d3dContext->Unmap(stagingTexture, 0);

    // Only accumulate pixel-work time, not Map stall, to keep stats meaningful
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    m_scalerTotalUs += us;
    m_scalerMapTotalUs += mapUs;
    m_scalerFrameCount++;
    auto now = std::chrono::steady_clock::now();
    if (m_scalerFrameCount > 0 && std::chrono::duration_cast<std::chrono::seconds>(now - m_scalerLastLogTime).count() >= 10) {
        std::stringstream ss;
        ss << "Scaler stats [" << m_scalerMethodName << "]: avg=" << (m_scalerTotalUs / m_scalerFrameCount)
           << "us map=" << (m_scalerMapTotalUs / m_scalerFrameCount)
           << "us over " << m_scalerFrameCount << " frames";
        LOGI(TAG, ss.str());
        m_scalerTotalUs = 0;
        m_scalerMapTotalUs = 0;
        m_scalerFrameCount = 0;
        m_scalerLastLogTime = now;
    }
    return true;
}

bool WindowsScreenCapture::CaptureScaleCPU(unsigned char* dst, int dstW, int dstH)
{
    ID3D11Texture2D* staging = AcquireNextDesktopFrameForCPU();
    if (!staging) { 
        return false; 
    }
    return ScaleOnCPU(staging, dstW, dstH, dst);
}

bool WindowsScreenCapture::CaptureScaleGPU(unsigned char* dst, int dstW, int dstH)
{
    ID3D11Texture2D* tex = AcquireNextDesktopFrame();
    if (!tex) { 
        return false; 
    }
    ID3D11Texture2D* staging = GetScaledStagingTexture(tex, dstW, dstH);
    if (!staging) { 
        LOGE(TAG, "CaptureScaleGPU: GetScaledStagingTexture failed"); 
        return false; 
    }
    return CopyStagingToReusableBitmap(staging, dstW, dstH, dst);
}

bool WindowsScreenCapture::CaptureNoScale(unsigned char* dst, int dstW, int dstH)
{
    ID3D11Texture2D* staging = AcquireNextDesktopFrameForCPU();
    if (!staging) { 
        return false; 
    }
    D3D11_TEXTURE2D_DESC desc;
    staging->GetDesc(&desc);
    return CopyStagingToReusableBitmap(staging, (int)desc.Width, (int)desc.Height, dst);
}

bool WindowsScreenCapture::CopyNV12StagingToBuffer(ID3D11Texture2D* staging, int width, int height, unsigned char* dst)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_d3dContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        LOGE(TAG, "CopyNV12StagingToBuffer: failed to map staging texture"); return false;
    }

    uint8_t* pSrc = (uint8_t*)mapped.pData;
    uint8_t* pDstY = dst;
    uint8_t* pDstUV = dst + (size_t)width * height;

    // Copy Y plane line by line to remove RowPitch padding
    for (int i = 0; i < height; ++i) {
        memcpy(pDstY + (size_t)i * width, pSrc + (size_t)i * mapped.RowPitch, width);
    }

    // Copy UV plane line by line
    uint8_t* pSrcUV = pSrc + (size_t)mapped.RowPitch * height;
    for (int i = 0; i < height / 2; ++i) {
        memcpy(pDstUV + (size_t)i * width, pSrcUV + (size_t)i * mapped.RowPitch, width);
    }

    m_d3dContext->Unmap(staging, 0);
    return true;
}

bool WindowsScreenCapture::CaptureAndConvertToReusableBitmap(unsigned char* dst, int dstW, int dstH)
{
    if (m_config.outputFormat == ImageUtils::PixelFormat::NV12) {
        ID3D11Texture2D* tex = AcquireNextDesktopFrame();
        if (!tex) { 
             return false; 
        }
        ID3D11Texture2D* staging = ScaleTextureVPE(tex, dstW, dstH, DXGI_FORMAT_NV12);
        if (!staging) { LOGE(TAG, "CaptureAndConvertToReusableBitmap(NV12): ScaleTextureVPE failed"); return false; }
        
        // Optimization: Pass dst directly to avoid extra vector allocation and copy
        return CopyNV12StagingToBuffer(staging, dstW, dstH, dst);
    }
    if (m_config.scale == 1.0f)                   return CaptureNoScale(dst, dstW, dstH);
    if (m_config.scaleMethod == SCALE_METHOD_CPU)  return CaptureScaleCPU(dst, dstW, dstH);
    return CaptureScaleGPU(dst, dstW, dstH);
}

void WindowsScreenCapture::DrawCursor(HDC hdc, int screenWidth, int screenHeight)
{
    CURSORINFO cursorInfo = {0};
    cursorInfo.cbSize = sizeof(CURSORINFO);

    if (!GetCursorInfo(&cursorInfo) || !(cursorInfo.flags & CURSOR_SHOWING)) {
        return;
    }

    ICONINFO iconInfo;
    if (!GetIconInfo(cursorInfo.hCursor, &iconInfo)) {
        return;
    }

    // Calculate cursor position with scaling
    int cursorX = (int)(cursorInfo.ptScreenPos.x * m_config.scale) - iconInfo.xHotspot;
    int cursorY = (int)(cursorInfo.ptScreenPos.y * m_config.scale) - iconInfo.yHotspot;

    // Draw cursor icon
    DrawIconEx(hdc, cursorX, cursorY, cursorInfo.hCursor, 0, 0, 0, NULL, DI_NORMAL);

    // Clean up
    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
}

std::unique_ptr<IScreenCapture> CreateScreenCapture(Platform* platform) {
    return std::make_unique<WindowsScreenCapture>(platform);
}
