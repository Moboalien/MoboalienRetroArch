#pragma once
#ifdef _WIN32
#include "avcodec_pipeline.h"
#include "gpu_color_converter.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000 // Target Windows 10
#endif

#include <windows.h>
#include <d3d11_3.h>
#include <dxgi1_2.h> // Required for modern formats like NV12
#include <deque>

#ifdef USE_FFMPEG
extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/hwcontext_qsv.h>
}
#endif

struct NV12TextureSet {
    AVFrame* frame = nullptr;
    ID3D11Texture2D* nv12Texture = nullptr;
    ID3D11UnorderedAccessView* yUAV = nullptr;
    ID3D11UnorderedAccessView* uvUAV = nullptr;
    ID3D11Query* copyQuery = nullptr;  // signals when CopySubresourceRegion is done
    bool IsCopyComplete(ID3D11DeviceContext* ctx) const {
        if (!copyQuery) return true;
        BOOL done = FALSE;
        return ctx->GetData(copyQuery, &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && done;
    }
};


class AvcodecGpuPipeline : public AvcodecPipeline {
public:
    AvcodecGpuPipeline(Platform* platform);
    ~AvcodecGpuPipeline();
    bool Initialize(IScreenCapture* capture, int width, int height, int fps, int quality, float scale = 1.0f) override;
protected:
    AVFrame* PrepareFrame(IScreenCapture* capture, int64_t& pts) override;
    void FinishFrame(AVFrame* frame) override;
    bool SetupNV12TextureWithUAVs(NV12TextureSet* slot);
    bool InitializeD3D11Device();
    bool InitializeQSVDevice();
    bool InitializeFramesContext(int width, int height);
    bool InitializeRingBuffer();
    bool InitializeEncoder(const AVCodec* codec, int quality);
    bool ConvertBGRAToNV12(ID3D11Texture2D* bgra, NV12TextureSet& slot);
    bool CreateNV12Slot();
    void CleanupRingBuffer();
    bool HandleDimensionChange(int newWidth, int newHeight);
    void CleanupSlot(NV12TextureSet& slot);
    void DrainPendingRelease();
    static constexpr int nv12PoolSize = 4;
    static constexpr int maxNv12PoolSize = nv12PoolSize * 2;
    std::vector<NV12TextureSet> m_nv12Pool;
    int GetCurrentNv12SlotCount() const;
    std::deque<NV12TextureSet> m_pendingRelease;
    bool AvcodecGpuPipeline::IsValidQsvFrame(AVFrame* frame) {
        if (!frame) return false;
        if (frame->format != AV_PIX_FMT_QSV) return false;
        if (!frame->hw_frames_ctx) return false;
        if (!frame->data[3]) return false;
        return true;
    }
private:
    AVBufferRef* m_hw_device_ctx = nullptr;
    AVBufferRef* m_hw_frames_ctx = nullptr;
    AVBufferRef* m_d3d11_hw_dev_ctx = nullptr;
    AVBufferRef* m_d3d11_frames_ctx = nullptr;
    AVBufferRef* qsv_hw_dev_ctx = nullptr;
    ID3D11DeviceContext* m_d3dContext = nullptr;
    ID3D11Device* m_d3dDevice = nullptr;
    GPUColorConverter m_colorConverter;
    int m_width = 0;
    int m_height = 0;
    int m_captureWidth = 0;
    int m_captureHeight = 0;

};
#endif