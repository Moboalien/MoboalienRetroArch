#pragma once

#include <vector>
#include <cstdint>
#include "platform.h"

class IScreenCapture;

struct EncodedFrameInfo {
    std::vector<char> data;
    bool isKeyFrame = false;
    bool hasConfig = false;
    int64_t pts = 0;
};

class IEncodingPipeline {
public:
    virtual ~IEncodingPipeline() = default;
    
    virtual bool Initialize(IScreenCapture* capture, int width, int height, int fps, int quality, float scale = 1.0f) = 0;
    virtual float GetScale() const = 0;
    virtual int GetQuality() const = 0;
    virtual bool ProcessFrame(IScreenCapture* capture, EncodedFrameInfo& outInfo, bool forceKeyframe) = 0;
    virtual bool SubmitFrameForEncoding(IScreenCapture* capture, bool forceKeyframe) = 0;
    virtual bool ReceiveEncodedFrames(EncodedFrameInfo& outInfo) = 0;
    virtual const std::vector<uint8_t>& GetSpsPps() const = 0;
    virtual void Cleanup() = 0;
    virtual void EnableStreamSaving(bool enable) = 0;
};

enum class PipelineType {
    CPU,
    GPU
};

class PipelineFactory {
public:
    static IEncodingPipeline* Create(PipelineType type, Platform* platform);
};
