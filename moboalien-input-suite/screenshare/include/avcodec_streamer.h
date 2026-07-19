#pragma once
#include "udp_streamer.h"
#include <memory>
#include <vector>
#include <string>

class AvcodecPipeline;

class AvcodecStreamer : public UdpStreamer {
public:
    AvcodecStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder);
    ~AvcodecStreamer();

    void Stop() override;

    void SetForceCpuEncoding(bool force);
    void EnableStreamSaving(bool enable);

protected:
    bool CaptureAndEncode(int quality) override;

    void ServerLoop() override;
    size_t FlushSendBuffers() override;
    void OnConfigUpdated(const StreamerConfig& sc, const CaptureConfig& cc) override;

private:
    bool InitializePipeline(int width, int height, float scale);
    bool CaptureAndEncodeAndSend(int quality);

    std::unique_ptr<AvcodecPipeline> m_pipeline;
    uint64_t m_lastKeyFrameTime = 0;
    bool m_forceCpuEncoding = false;
    bool m_saveStream = false;
};
