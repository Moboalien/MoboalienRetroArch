#pragma once
#include "tcp_streamer.h"
#include "bitmap_differ.h"

class DifferentialStreamer : public TcpStreamer {
public:
    DifferentialStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder);
    ~DifferentialStreamer();
protected:
    void HandleClient(uintptr_t client) override;
    void OnPerfLogReset() override {
        m_totalCaptureTimeMs = m_totalDiffTimeMs = m_totalEncodeTimeMs = m_totalCursorTimeMs = 0.0;
    }
private:
    bool BuildCursorPacket(IScreenCapture* capture, std::vector<char>& outPacket);
    bool CaptureAndEncode(int quality) override;
    void EncodeFramePacket(std::vector<char>& outPacket, const DiffResult& diff, const unsigned char* jpegData, size_t jpegSize);
    void EncodeCursorPacket(std::vector<char>& outPacket, const CursorFrame& cursorFrame, unsigned char* pngData, size_t pngSize);

    BitmapDiffer* m_bitmapDiffer;
    int m_streamFrameCount;
    
    // Performance accumulators (reset via OnPerfLogReset)
    double m_totalCaptureTimeMs;
    double m_totalDiffTimeMs;
    double m_totalEncodeTimeMs;
    double m_totalCursorTimeMs;

    // Previous cursor state for differential updates
    int m_lastX;
    int m_lastY;
    bool m_lastVisibility;
    uint32_t m_lastCursorHash;
};