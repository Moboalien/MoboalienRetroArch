#pragma once
#include "video_config.h"
#include "screen_capture.h"
#include "i_image_encoder.h"
#include "platform.h"
#include <vector>
#include <mutex>
#include <string>
#include <unordered_map>

extern std::unordered_map<std::string, std::string> g_perfMetrics;

#define CLIENT_TIMEOUT_MS 10000

class BaseStreamer {
public:
    BaseStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder);
    virtual ~BaseStreamer();

    virtual bool Start();
    virtual void Stop();
    bool HasClients() const;
    uint16_t GetPort();
    int GetClientCount() const;
    virtual int GetCongestedClientCount() const { return 0; }

    virtual bool UpdateConfig(const StreamerConfig& sc, const CaptureConfig& cc);
    virtual StreamingMode GetStreamingMode() const { return m_config.streamingMode; }

protected:
    virtual void OnConfigUpdated(const StreamerConfig& sc, const CaptureConfig& cc) {}
    virtual void OnPerfLogReset() {}

    virtual void ServerLoop() = 0;
    virtual size_t FlushSendBuffers() { return 0; }
    virtual bool CaptureAndEncode(int quality) = 0;
    virtual void OnBeforeFrame() {} // Hook for per-frame work before encode (e.g. polling client requests)

    virtual void StreamingLoop();
    void AdjustQuality(int& adaptiveQuality, uint64_t& lastQualityAdjustTime);
    void ThrottleFps(uint64_t frameStartTime);
    void LogPerformanceMetrics(int adaptiveQuality);
    std::string FormatPerfString();

    bool isKeyframeRequested();
    void setKeyframeRequested(bool requested);

    Platform* m_platform;
    StreamerConfig m_config;
    IScreenCapture* m_capture;
    IImageEncoder* m_imageEncoder;

    uintptr_t m_socket;
    volatile bool m_running;
    int m_clientCount;
    Platform::ThreadHandle m_thread;
    Platform::ThreadHandle m_streamingThread;

    uint64_t m_shutdownCallbackId = 0;
    bool m_keyframeRequestPending;
    double m_achievedFps;

    uint64_t m_lastPerfLogTime;
    uint64_t m_totalCaptureAndEncodeTime = 0;
    int m_framesSincePerfLog;
    size_t m_totalBytesSent;
    const int LOG_INTERVAL_MS = 10000;

    static void* ServerThreadProc(void* param);
    static void* StreamingThreadProc(void* param);
};