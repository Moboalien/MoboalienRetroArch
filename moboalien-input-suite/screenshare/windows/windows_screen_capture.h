#pragma once

#define NOMINMAX
#include "screen_capture.h"
#include "platform.h"
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <memory>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

class WindowsScreenCapture : public IScreenCapture {
    static constexpr uint64_t CAPTURE_IDLING_THRESHOLD = 3000;
public:
    explicit WindowsScreenCapture(Platform* platform);
    ~WindowsScreenCapture();

    bool Initialize(const CaptureConfig& config) override;
    ImageUtils::RawImageFrame CaptureFrame(bool withCursor = true, ImageUtils::PixelFormat format = ImageUtils::PixelFormat::BGRA32) override;
    CursorFrame GetCursorFrame() override;

    // IScreenCapture overrides for Hardware Encoding
    void* GetRenderDevice() override;
    void* GetCaptureTexture() override;
    void SetCustomDevice(void* device) override;
    float GetScale() const override;
    uint64_t GetLastCaptureTimestamp() override;

private:
    bool InitializeD3D();
    bool InitializeDuplication();
    void CleanupD3D();
    void StartCaptureThread();
    void StopCaptureThread();
    static void* CaptureThreadProc(void* param);
    void CaptureThreadLoop();
    void SaveBitmapToFile(HBITMAP hBitmap, const char* filename);

    bool CaptureAndConvertToReusableBitmap(unsigned char* dst, int dstW, int dstH);
    bool CaptureScaleCPU(unsigned char* dst, int dstW, int dstH);
    bool CaptureScaleGPU(unsigned char* dst, int dstW, int dstH);
    bool CaptureNoScale(unsigned char* dst, int dstW, int dstH);
    ID3D11Texture2D* GetScaledStagingTexture(ID3D11Texture2D* texture, int dstW, int dstH);
    ID3D11Texture2D* ScaleWithVPE(ID3D11Texture2D* texture, int dstW, int dstH);
    ID3D11Texture2D* ScaleWithShader(ID3D11Texture2D* texture, int dstW, int dstH);
    bool CopyNV12StagingToBuffer(ID3D11Texture2D* staging, int width, int height, unsigned char* dst);
    bool ScaleOnCPU(ID3D11Texture2D* texture, int dstW, int dstH, unsigned char* dst);
    void DrawCursor(HDC hdc, int screenWidth, int screenHeight);
    HBITMAP CloneReusableBitmap();
    void EnsureReusableBitmap(int width, int height);
    bool InitGPUScaler();
    bool InitVPE();
    ID3D11Texture2D* ScaleTextureOnGPU(ID3D11Texture2D* src, int dstW, int dstH);
    ID3D11Texture2D* ScaleTextureVPE(ID3D11Texture2D* src, int dstW, int dstH, DXGI_FORMAT format);
    bool CopyStagingToReusableBitmap(ID3D11Texture2D* staging, int width, int height, unsigned char* dst);

    // Private method to get the D3D11 texture for hardware encoding
    ID3D11Texture2D* CaptureD3D11Texture();
    ID3D11Texture2D* AcquireNextDesktopFrame();
    ID3D11Texture2D* AcquireNextDesktopFrameForCPU();

    bool IsCaptureIdle() {
        if (++m_idleCheckCounter % 128) return false;
        uint64_t last = m_lastCaptureFrameTime.load(std::memory_order_relaxed);
        return last != 0 && GetTickCount64() - last >= CAPTURE_IDLING_THRESHOLD;
    }

    CaptureConfig m_config;
    HDC m_screenDC;
    Platform* m_platform;  // non-owning

    // D3D11 resources
    ID3D11Device* m_d3dDevice;
    ID3D11DeviceContext* m_d3dContext;
    IDXGIOutputDuplication* m_duplication;
    HDC m_memoryDC;
    HBITMAP m_reusableBitmap;
    ID3D11Texture2D* m_sharedTexture = nullptr;
    ID3D11Texture2D* m_sharedStagingTexture = nullptr; // non-null sentinel
    std::vector<ID3D11Texture2D*> m_sharedStagingTextures;
    unsigned int m_sharedStagingFrameIndex = 0;
    void* m_bitmapData;

    // GPU scaler resources
    ID3D11VertexShader*   m_scaleVS = nullptr;
    ID3D11PixelShader*    m_scalePS = nullptr;
    ID3D11SamplerState*   m_linearSampler = nullptr;
    ID3D11RasterizerState* m_rasterizerState = nullptr;
    ID3D11Texture2D*      m_scaledTexture = nullptr;
    ID3D11RenderTargetView* m_scaledRTV = nullptr;
    ID3D11ShaderResourceView* m_scaledSRV = nullptr;
    ID3D11Texture2D*      m_scaledStaging[2] = { nullptr, nullptr };
    int m_scaledW = 0, m_scaledH = 0;
    unsigned int m_scaledFrameIndex = 0;

    // NV12 VPE pipeline (separate from BGRA VPE — different output format)
    ID3D11VideoDevice*              m_videoDevice = nullptr;
    ID3D11VideoContext*             m_videoContext = nullptr;
    ID3D11VideoProcessor*           m_vpProcessor = nullptr;
    ID3D11VideoProcessorEnumerator* m_vpEnum = nullptr;
    ID3D11VideoProcessorOutputView* m_vpOutputView = nullptr;
    ID3D11Texture2D*                m_vpOutputTex = nullptr;
    ID3D11Texture2D*                m_vpStaging[2] = { nullptr, nullptr };
    int m_vpSrcW = 0, m_vpSrcH = 0;
    int m_vpDstW = 0, m_vpDstH = 0;
    DXGI_FORMAT m_vpFormat = DXGI_FORMAT_UNKNOWN;
    unsigned int m_vpFrameIndex = 0;

    bool m_scaledVpeLogged = false;
    bool m_scaledShaderLogged = false;

    // Scaler perf stats
    uint64_t m_scalerTotalUs = 0;
    uint64_t m_scalerMapTotalUs = 0;
    uint32_t m_scalerFrameCount = 0;
    std::chrono::steady_clock::time_point m_scalerLastLogTime = {};
    std::string m_scalerMethodName;
    std::vector<int>     m_srcXTable;     // integer part of source X
    std::vector<uint8_t> m_srcXFracTable; // fractional part of source X (0-255)
    int   m_srcXTableWidth = 0;
    float m_srcScaleCached = 0.0f;

    HBITMAP m_cursorBitmap;
    void* m_cursorBitmapData;
    HDC m_cursorDC;
    int m_cursorWidth = 0, m_cursorHeight = 0;

    // Double-buffered background capture
    struct FrameBuffer {
        std::shared_ptr<std::vector<unsigned char>> data;
        int width = 0, height = 0, stride = 0;
        ImageUtils::PixelFormat format = ImageUtils::PixelFormat::BGRA32;
        uint64_t timestampMs = 0;
    };
    FrameBuffer m_frameBuffers[2];
    int m_writeIdx = 0;          // index being written by capture thread
    std::mutex m_frameMutex;
    Platform::ThreadHandle m_captureThread = nullptr;
    std::atomic<bool> m_captureRunning{ false };
    std::atomic<uint64_t> m_lastCaptureFrameTime{ 0 };
    uint32_t m_idleCheckCounter = 0;
    bool m_timeoutFlushDone = false;
    bool m_timeoutFlushDoneCPU = false;
};