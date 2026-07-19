#define NOMINMAX
#include "avcodec_streamer.h"
#include "windows/avcodec_cpu_pipeline.h"
// For GPU pipeline, Currently disabled
// #ifdef _WIN32
// #include "windows/avcodec_gpu_pipeline.h"
// #endif
#include "utils.h"
#include <string>
#include "signal_handler.h"

// UDP Packet Header Constants
const uint8_t PACKET_TYPE_FRAME = 0;
const uint8_t PACKET_TYPE_HELLO = 1;

const uint8_t FLAG_KEYFRAME = 0x01;
const uint8_t FLAG_CONFIG   = 0x02;

const int UDP_MAX_PAYLOAD = 1400;
const int UDP_HEADER_SIZE = 22;

static const char* TAG = "AvcodecStreamer";

AvcodecStreamer::AvcodecStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder)
    : UdpStreamer(config, capture, platform, imageEncoder) {}

AvcodecStreamer::~AvcodecStreamer() {
    Stop();
}

void AvcodecStreamer::SetForceCpuEncoding(bool force) {
    m_forceCpuEncoding = force;
}

void AvcodecStreamer::EnableStreamSaving(bool enable) {
    if (m_pipeline) {
        m_pipeline->EnableStreamSaving(enable);
    }
    m_saveStream = enable;
}

void AvcodecStreamer::Stop() {
    UdpStreamer::Stop();
    m_pipeline.reset();
}

void AvcodecStreamer::OnConfigUpdated(const StreamerConfig& sc, const CaptureConfig& cc) {
    if (m_pipeline && (sc.scale != m_pipeline->GetScale() || sc.adaptiveQuality.quality != m_pipeline->GetQuality())) {
        LOGI(TAG, "Scale or quality changed, resetting pipeline");
        m_pipeline.reset();
    }
}

void AvcodecStreamer::ServerLoop() {
    char buffer[1024];
    while (m_running && !SignalHandler::isShutdownRequested()) {
        std::string clientIP;
        int clientPort;
        int bytes = m_platform->RecvFrom(m_socket, buffer, sizeof(buffer), 0, clientIP, clientPort);

        if (bytes > 0) {
            if (buffer[0] == PACKET_TYPE_HELLO) {
                std::lock_guard<std::mutex> lock(m_udpClientsLock);
                bool found = false;
                for (auto& client : m_udpClients) {
                    if (client.ip == clientIP && client.port == clientPort) {
                        client.lastHeartbeat = m_platform->GetTickCountMs();
                        client.active = true;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    LOGI(TAG, "New UDP client: " + clientIP + ":" + std::to_string(clientPort));
                    m_udpClients.push_back({clientIP, clientPort, true, m_platform->GetTickCountMs()});
                    m_clientCount = (int)m_udpClients.size();
                    setKeyframeRequested(true);

                    if (m_pipeline) {
                        const auto& spsPps = m_pipeline->GetSpsPps();
                        if (!spsPps.empty()) {
                            std::vector<char> configPacket;
                            configPacket.push_back(PACKET_TYPE_FRAME);
                            configPacket.push_back(FLAG_CONFIG);
                            AppendInt32(configPacket, -1);
                            AppendInt64(configPacket, -1);
                            configPacket.insert(configPacket.end(), (const char*)spsPps.data(), (const char*)spsPps.data() + spsPps.size());
                            m_platform->SendTo(m_socket, configPacket.data(), (int)configPacket.size(), 0, clientIP, clientPort);
                        }
                    }
                }
            }
        }
    }
}

bool AvcodecStreamer::CaptureAndEncode(int quality) {
    size_t dataSent = FlushSendBuffers();
    bool success = CaptureAndEncodeAndSend(quality);
    m_totalBytesSent += (dataSent + FlushSendBuffers());
    return success;
}

size_t AvcodecStreamer::FlushSendBuffers() {
    if (!m_pipeline) {
        LOGE(TAG, "FlushSendBuffers: pipeline is null");
        return 0;
    }
    
    EncodedFrameInfo info;
    if (!m_pipeline->ReceiveEncodedFrames(info)) return 0;

    if (info.data.empty()) {
        LOGE(TAG, "FlushSendBuffers: received empty frame data");
        return 0;
    }

    if (info.isKeyFrame) {
        m_lastKeyFrameTime = m_platform->GetTickCountMs();
        setKeyframeRequested(false);
    }

    uint8_t flags = 0;
    if (info.isKeyFrame) flags |= FLAG_KEYFRAME | FLAG_CONFIG;

    const char* data = info.data.data();
    size_t totalSize = info.data.size();
    uint32_t frameId = m_frameId++;
    int64_t pts = info.pts;
    uint16_t totalFragments = (uint16_t)((totalSize + UDP_MAX_PAYLOAD - 1) / UDP_MAX_PAYLOAD);

    std::lock_guard<std::mutex> lock(m_udpClientsLock);
    EvictTimedOutClients();

    for (const auto& client : m_udpClients) {
        if (!client.active) continue;
        for (uint16_t frag = 0; frag < totalFragments; ++frag) {
            size_t offset = frag * UDP_MAX_PAYLOAD;
            size_t chunkSize = std::min((size_t)UDP_MAX_PAYLOAD, totalSize - offset);

            std::vector<char> udpPacket;
            udpPacket.reserve(UDP_HEADER_SIZE + chunkSize);
            udpPacket.push_back(PACKET_TYPE_FRAME);
            udpPacket.push_back(flags);
            AppendInt32(udpPacket, frameId);
            AppendInt64(udpPacket, pts);
            AppendInt16(udpPacket, frag);
            AppendInt16(udpPacket, totalFragments);
            AppendInt16(udpPacket, (uint16_t)chunkSize);
            AppendInt16(udpPacket, 0);
            udpPacket.insert(udpPacket.end(), data + offset, data + offset + chunkSize);
            m_platform->SendTo(m_socket, udpPacket.data(), (int)udpPacket.size(), 0, client.ip, client.port);
        }
    }

    m_totalBytesSent += totalSize;
    m_framesSincePerfLog++;
    return totalSize;
}

bool AvcodecStreamer::InitializePipeline(int width, int height, float scale) {
    m_pipeline.reset();

    auto cpuPipeline = std::make_unique<AvcodecCpuPipeline>(m_platform);
    cpuPipeline->EnableStreamSaving(m_saveStream);
    if (cpuPipeline->Initialize(m_capture, width, height, m_config.fps, m_config.adaptiveQuality.quality, scale)) {
        m_pipeline = std::move(cpuPipeline);
        LOGI(TAG, "Initialized CPU Pipeline");
        return true;
    }

    LOGE(TAG, "Failed to initialize any pipeline");
    return false;
}

bool AvcodecStreamer::CaptureAndEncodeAndSend(int quality) {
    if (!m_pipeline) {
        int width = 1920, height = 1080;
        m_platform->GetScreenDimensions(width, height);
        if (!InitializePipeline(width, height, m_config.scale)) return false;
    }

    uint64_t now = m_platform->GetTickCountMs();
    bool keyframe_was_requested = isKeyframeRequested();
    if (!keyframe_was_requested && (m_lastKeyFrameTime == 0 || now - m_lastKeyFrameTime > 2000)) {
        keyframe_was_requested = true;
        setKeyframeRequested(true);
    }
    if (keyframe_was_requested) {
        LOGV(TAG, "Keyframe requested for frameId " + std::to_string(m_frameId));
    }
    return m_pipeline->SubmitFrameForEncoding(m_capture, keyframe_was_requested);
}
