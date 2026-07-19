#pragma once
#include "tcp_streamer.h"
#include "screen_capture.h"

class HTTPStreamer : public TcpStreamer {
public:
    HTTPStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder);
    virtual ~HTTPStreamer();

protected:
    void HandleClient(uintptr_t client) override;
    bool CaptureAndEncode(int quality) override;
    void OnPerfLogReset() override {
        m_totalCaptureTimeMs = m_totalEncodeTimeMs = 0.0;
    }

private:
    void SendHttpResponse(uintptr_t clientSocket);

    double m_totalCaptureTimeMs;
    double m_totalEncodeTimeMs;
};