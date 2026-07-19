#pragma once
#include <memory>
#include "image_utils.h"

class Platform;

enum CaptureMethod {
    CAPTURE_DESKTOP_DUPLICATION = 0,
    CAPTURE_GDI = 1,
};

enum ScaleMethod {
    SCALE_METHOD_AUTO   = 0, // Try VPE first, fall back to shader, then CPU
    SCALE_METHOD_SHADER = 1, // Always use shader blit
    SCALE_METHOD_VPE    = 2, // Always use Video Processing Engine
    SCALE_METHOD_CPU    = 3, // Always use CPU nearest-neighbor (no GPU required)
};

struct CaptureConfig {
    float scale;
    CaptureMethod method;
    ScaleMethod scaleMethod;
    int minFps;
    int stagingFrameCount;
    ImageUtils::PixelFormat outputFormat;
    CaptureConfig() : scale(1.0f), method(CAPTURE_DESKTOP_DUPLICATION), scaleMethod(SCALE_METHOD_AUTO), minFps(20), stagingFrameCount(2), outputFormat(ImageUtils::PixelFormat::BGRA32) {}
};

struct CursorFrame {
    ImageUtils::RawImageFrame image;
    int x;
    int y;
    bool isVisible;
    bool isMonochrome;
    uint32_t monochromeCursorHash; // Added to track image changes
    CursorFrame() : x(0), y(0), isVisible(false), isMonochrome(false) {}
};

class IScreenCapture {
public:
    virtual ~IScreenCapture() {}
    virtual bool Initialize(const CaptureConfig& config) = 0;
    virtual ImageUtils::RawImageFrame CaptureFrame(bool withCursor = true, ImageUtils::PixelFormat format = ImageUtils::PixelFormat::BGRA32) = 0;
    // Returns the current cursor image and its screen position.
    // The image data is owned by the IScreenCapture implementation and is valid
    // until the next call to CaptureFrame or GetCursorFrame.
    virtual CursorFrame GetCursorFrame() = 0;

    // GPU/Hardware-accelerated capture access.
    // Return a platform-specific device pointer (e.g., ID3D11Device* on Windows).
    virtual void* GetRenderDevice() { return nullptr; }
    // Return a platform-specific texture pointer (e.g., ID3D11Texture2D* on Windows).
    virtual void* GetCaptureTexture() { return nullptr; }
    // Set a custom device for rendering (e.g., ID3D11Device* on Windows).
    virtual void SetCustomDevice(void* device) {}

    virtual float GetScale() const = 0;
    virtual uint64_t GetLastCaptureTimestamp() { return 0; }
};

/// <summary>
/// Creates a platform-specific instance of an IScreenCapture object.
/// </summary>
/// <returns>A pointer to a new IScreenCapture instance, or nullptr on failure.</returns>
std::unique_ptr<IScreenCapture> CreateScreenCapture(Platform* platform);