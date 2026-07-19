#define NOMINMAX
#include "base_streamer.h"
#include "video_config.h"
#include "platform.h"
#include "signal_handler.h"
#include "utils.h"
#include "packet_types.h"
#include <string>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <atomic>
#include <unordered_map>

std::unordered_map<std::string, std::string> g_perfMetrics;

static const char* TAG = "BaseStreamer";

BaseStreamer::BaseStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder)
    : m_config(config), m_capture(capture),
      m_socket(Platform::INVALID_SOCKET_HANDLE), m_running(false), m_clientCount(0),
      m_platform(platform), m_imageEncoder(imageEncoder),
      m_keyframeRequestPending(false), m_achievedFps(0.0),
      m_lastPerfLogTime(0), m_framesSincePerfLog(0), m_totalBytesSent(0) {
    m_lastPerfLogTime = m_platform->GetTickCountMs();
}

BaseStreamer::~BaseStreamer() {
    Stop();
}

uint16_t BaseStreamer::GetPort() {
    return m_config.port;
}

bool BaseStreamer::Start() {
    if (!m_platform->SocketsInitialize()) {
        LOGE(TAG, "Sockets initialization failed");
        return false;
    }
    return true;
}

void BaseStreamer::Stop() {
    // Signal threads to exit and unblock any blocking socket calls.
    m_running = false;

    if (m_shutdownCallbackId != 0) {
        SignalHandler::getInstance().unregisterCallback(m_shutdownCallbackId);
        m_shutdownCallbackId = 0;
    }

    if (m_socket != Platform::INVALID_SOCKET_HANDLE) {
        m_platform->CloseSocket(m_socket);
        m_socket = Platform::INVALID_SOCKET_HANDLE;
    }
}

bool BaseStreamer::HasClients() const { return m_clientCount > 0; }
int BaseStreamer::GetClientCount() const { return m_clientCount; }

void BaseStreamer::StreamingLoop() {
    if (!m_platform->EnableMMCSSForCurrentThread()) {
        m_platform->SetCurrentThreadHighPriority();
    }

    int adaptiveQuality = m_config.adaptiveQuality.quality;
    uint64_t qualityAdjustTime = 0;
    int tickMs = 1;
    m_platform->SetTimerResolution(tickMs);

    while (m_running && !SignalHandler::isShutdownRequested()) {
        if (!HasClients()) {
            m_platform->Sleep(100);
            m_framesSincePerfLog = 0;
            continue;
        }

        uint64_t frameStartTime = m_platform->GetTickCountMs();
        OnBeforeFrame();
        AdjustQuality(adaptiveQuality, qualityAdjustTime);

        uint64_t encodeStart = m_platform->GetTickCountMs();
        CaptureAndEncode(adaptiveQuality);
        m_totalCaptureAndEncodeTime += (m_platform->GetTickCountMs() - encodeStart);

        LogPerformanceMetrics(adaptiveQuality);
        ThrottleFps(frameStartTime);
    }
}

void BaseStreamer::AdjustQuality(int& adaptiveQuality, uint64_t& lastQualityAdjustTime) {
    int congestedClients = GetCongestedClientCount();
    int totalClients = GetClientCount();
    uint64_t currentTime = m_platform->GetTickCountMs();
    adaptiveQuality = std::min(adaptiveQuality, m_config.adaptiveQuality.quality);
    if (currentTime - lastQualityAdjustTime > 2000) {
        if (congestedClients > totalClients / 2 && adaptiveQuality > m_config.adaptiveQuality.minQuality) {
            adaptiveQuality -= m_config.adaptiveQuality.qualityStep * 2;
            adaptiveQuality = std::max(adaptiveQuality, m_config.adaptiveQuality.minQuality);
            lastQualityAdjustTime = currentTime;
            LOGI(TAG, "Quality : " + std::to_string(adaptiveQuality));
        } else if (congestedClients <= totalClients / 2 && adaptiveQuality < m_config.adaptiveQuality.quality) {
            adaptiveQuality += m_config.adaptiveQuality.qualityStep;
            adaptiveQuality = std::min(adaptiveQuality, m_config.adaptiveQuality.quality);
            lastQualityAdjustTime = currentTime;
            LOGI(TAG, "Quality : " + std::to_string(adaptiveQuality));
        }
    }
}

void BaseStreamer::ThrottleFps(uint64_t frameStartTime) {
    int targetFrameTime = 1000 / m_config.fps;
    uint64_t frameEndTime = m_platform->GetTickCountMs();
    uint64_t processingTime = frameEndTime - frameStartTime;
    int sleepDuration = targetFrameTime - static_cast<int>(processingTime);
    if (sleepDuration > 0) m_platform->Sleep(sleepDuration);
}

std::string BaseStreamer::FormatPerfString() {
    std::stringstream ss;
    bool first = true;
    for (auto& [k, v] : g_perfMetrics) {
        if (!first) ss << ", ";
        ss << k << ": " << v;
        first = false;
    }
    return ss.str();
}

void BaseStreamer::LogPerformanceMetrics(int adaptiveQuality) {
    uint64_t currentTime = m_platform->GetTickCountMs();
    uint64_t elapsedLogTime = currentTime - m_lastPerfLogTime;
    if (elapsedLogTime >= (uint64_t)LOG_INTERVAL_MS) {
        double achievedFps = (double)m_framesSincePerfLog * 1000.0 / elapsedLogTime;
        m_achievedFps = achievedFps;
        double bandwidthMBps = m_totalBytesSent / (elapsedLogTime / 1000.0 * 1000000.0);
        double avgBuildPacketTime = m_framesSincePerfLog > 0 ? (double)m_totalCaptureAndEncodeTime / m_framesSincePerfLog : 0.0;

        auto fmt1 = [](double v, int p) {
            std::ostringstream o; o << std::fixed << std::setprecision(p) << v; return o.str();
        };
        g_perfMetrics["FPS"]             = fmt1(achievedFps, 1) + " (target " + std::to_string(m_config.fps) + ")";
        g_perfMetrics["Quality"]          = std::to_string(adaptiveQuality);
        g_perfMetrics["Clients"]          = std::to_string(GetClientCount()) + " (" + std::to_string(GetCongestedClientCount()) + " slow)";
        g_perfMetrics["BW (MBps)"]        = fmt1(bandwidthMBps, 2);
        g_perfMetrics["CaptureEncode ms"] = fmt1(avgBuildPacketTime, 1);

        LOGI(TAG, FormatPerfString());
        m_lastPerfLogTime = currentTime;
        m_framesSincePerfLog = 0;
        m_totalBytesSent = 0;
        m_totalCaptureAndEncodeTime = 0;
        OnPerfLogReset();
    }
}

bool BaseStreamer::UpdateConfig(const StreamerConfig& sc, const CaptureConfig& cc) {
    if (sc.port != 0 && sc.port != m_config.port) {
        LOGE(TAG, "UpdateConfig: port change from " + std::to_string(static_cast<int>(m_config.port)) +
             " to " + std::to_string(static_cast<int>(sc.port)) + ". Restart required.");
        return false;
    }

    m_config = sc;

    if (m_capture) {
        if (!m_capture->Initialize(cc)) {
            LOGE(TAG, "UpdateConfig: Failed to initialize capture with new settings");
            return false;
        }
    }

    LOGI(TAG, "UpdateConfig: Applied new streamer config (fps=" + std::to_string(m_config.fps) +
         ", bitrate=" + std::to_string(m_config.bitrate / 1000) + "kbps" +
         ", quality=" + std::to_string(m_config.adaptiveQuality.quality) +
         ", scale=" + std::to_string(m_config.scale) + ")");
    OnConfigUpdated(sc, cc);
    return true;
}

bool BaseStreamer::isKeyframeRequested() {
    return m_keyframeRequestPending;
}

void BaseStreamer::setKeyframeRequested(bool requested) {
    m_keyframeRequestPending = requested;
}

void* BaseStreamer::ServerThreadProc(void* param) {
    BaseStreamer* self = static_cast<BaseStreamer*>(param);
    self->ServerLoop();
    return nullptr;
}

void* BaseStreamer::StreamingThreadProc(void* param) {
    BaseStreamer* self = static_cast<BaseStreamer*>(param);
    self->StreamingLoop();
    return nullptr;
}
