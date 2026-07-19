#define NOMINMAX
#include "udp_streamer.h"
#include "signal_handler.h"
#include "utils.h"
#include <string>
#include <algorithm>

static const char* TAG = "UdpStreamer";

UdpStreamer::UdpStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder)
    : BaseStreamer(config, capture, platform, imageEncoder) {}

UdpStreamer::~UdpStreamer() {
    Stop();
}

bool UdpStreamer::Start() {
    if (!BaseStreamer::Start()) return false;

    int requestedPort = static_cast<int>(GetPort());
    m_socket = m_platform->CreateUDPSocket(requestedPort);
    if (m_socket == Platform::INVALID_SOCKET_HANDLE) {
        LOGE(TAG, "Failed to create and bind UDP socket");
        m_platform->SocketsCleanup();
        return false;
    }

    int boundPort = m_platform->GetSocketPort(m_socket);
    m_config.port = static_cast<uint16_t>(boundPort);
    LOGI(TAG, "UDP server socket created at port " + std::to_string(static_cast<int>(GetPort())));

    m_running = true;

    if (m_shutdownCallbackId == 0) {
        m_shutdownCallbackId = SignalHandler::getInstance().registerCallback([this]() {
            LOGI(TAG, "UdpStreamer shutdown callback triggered");
            this->Stop();
        });
    }

    m_platform->CreateThread(&m_thread, &BaseStreamer::ServerThreadProc, this);
    m_platform->CreateThread(&m_streamingThread, &BaseStreamer::StreamingThreadProc, this);
    return true;
}

void UdpStreamer::Stop() {
    if (!m_running) return;

    BaseStreamer::Stop(); // sets m_running=false, closes m_socket, unregisters shutdown callback

    // Now safe to join — threads check m_running and the closed socket unblocks RecvFrom
    m_platform->JoinThread(m_streamingThread);
    m_platform->JoinThread(m_thread);

    m_platform->SocketsCleanup();
}

void UdpStreamer::EvictTimedOutClients() {
    // Must be called with m_udpClientsLock held
    uint64_t now = m_platform->GetTickCountMs();
    m_udpClients.erase(std::remove_if(m_udpClients.begin(), m_udpClients.end(),
        [this, now](const UdpClient& client) {
            if (now - client.lastHeartbeat > CLIENT_TIMEOUT_MS) {
                LOGI(TAG, "UDP client timed out: " + client.ip + ":" + std::to_string(client.port));
                return true;
            }
            return false;
        }), m_udpClients.end());
    m_clientCount = (int)m_udpClients.size();
}
