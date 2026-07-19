#define NOMINMAX
#include "tcp_streamer.h"
#include "signal_handler.h"
#include "utils.h"
#include "packet_types.h"
#include <string>
#include <algorithm>

static const char* TAG = "TcpStreamer";

TcpStreamer::TcpStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder)
    : BaseStreamer(config, capture, platform, imageEncoder) {}

TcpStreamer::~TcpStreamer() {
    Stop();
}

bool TcpStreamer::Start() {
    if (!BaseStreamer::Start()) return false;

    int requestedPort = static_cast<int>(GetPort());
    m_socket = m_platform->CreateListenSocket(requestedPort, 10);
    if (m_socket == Platform::INVALID_SOCKET_HANDLE) {
        LOGE(TAG, "Failed to create and bind listen socket");
        m_platform->SocketsCleanup();
        return false;
    }

    int boundPort = m_platform->GetSocketPort(m_socket);
    m_config.port = static_cast<uint16_t>(boundPort);
    LOGI(TAG, "TCP server socket created at port " + std::to_string(static_cast<int>(GetPort())));

    m_running = true;

    if (m_shutdownCallbackId == 0) {
        m_shutdownCallbackId = SignalHandler::getInstance().registerCallback([this]() {
            LOGI(TAG, "TcpStreamer shutdown callback triggered");
            this->Stop();
        });
    }

    m_platform->CreateThread(&m_thread, &BaseStreamer::ServerThreadProc, this);
    m_platform->CreateThread(&m_streamingThread, &BaseStreamer::StreamingThreadProc, this);
    m_platform->CreateThread(&m_flushThread, &TcpStreamer::FlushThreadProc, this);

    LOGI(TAG, "TCP buffer size: " + std::to_string(m_config.tcpBufferSize / 1024) + "KB");
    LOGI(TAG, "Max client buffer: " + std::to_string(m_config.maxClientBufferSize / (1024 * 1024)) + "MB");
    LOGI(TAG, "Quality: " + std::to_string(m_config.adaptiveQuality.minQuality) + "-" +
         std::to_string(m_config.adaptiveQuality.quality) + " Step: " + std::to_string(m_config.adaptiveQuality.qualityStep));
    return true;
}

void TcpStreamer::Stop() {
    if (!m_running) return;

    BaseStreamer::Stop(); // sets m_running=false, closes m_socket, unregisters shutdown callback

    // Now safe to join — threads check m_running and the closed socket unblocks AcceptConnection
    m_platform->JoinThread(m_streamingThread);
    m_platform->JoinThread(m_flushThread);
    m_platform->JoinThread(m_thread);

    {
        std::lock_guard<std::mutex> lock(m_clientsLock);
        for (int i = 0; i < m_clientCount; ++i) {
            if (m_clients[i].socket != Platform::INVALID_SOCKET_HANDLE)
                m_platform->CloseSocket(m_clients[i].socket);
        }
        m_clientCount = 0;
    }

    m_platform->SocketsCleanup();
}

int TcpStreamer::GetCongestedClientCount() const {
    std::lock_guard<std::mutex> lock(m_clientsLock);
    int count = 0;
    for (int i = 0; i < m_clientCount; ++i) {
        if (m_clients[i].sendBuffer.size() > (size_t)(m_config.maxClientBufferSize / 2))
            count++;
    }
    return count;
}

void TcpStreamer::RemoveClient(int index) {
    // Must be called with m_clientsLock held
    LOGI(TAG, "Client " + std::to_string(index) + " disconnected");
    m_platform->CloseSocket(m_clients[index].socket);
    if (index < m_clientCount - 1)
        m_clients[index] = m_clients[m_clientCount - 1];
    m_clientCount--;
}

void TcpStreamer::HandleClientPublic(uintptr_t client) { HandleClient(client); }

struct TcpClientData {
    TcpStreamer* streamer;
    uintptr_t client;
};

static void* TcpClientThreadProc(void* param)
{
    TcpClientData* data = (TcpClientData*)param;
    data->streamer->HandleClientPublic(data->client);
    delete data;
    return 0;
}

void TcpStreamer::ServerLoop() {
    while (m_running && !SignalHandler::isShutdownRequested()) {
        std::string clientIP;
        int clientPort;
        uintptr_t client = m_platform->AcceptConnection(m_socket, clientIP, clientPort);
        if (client != Platform::INVALID_SOCKET_HANDLE) {
            TcpClientData* data = new TcpClientData{this, client};
            LOGI(TAG, "Client connected from " + clientIP + ":" + std::to_string(clientPort));
            Platform::ThreadHandle clientThread{};
            if (m_platform->CreateThread(&clientThread, (Platform::ThreadRoutine)TcpClientThreadProc, data)) {
                m_platform->DetachThread(clientThread);
            } else {
                LOGE(TAG, "Failed to create client thread, closing connection");
                delete data;
                m_platform->CloseSocket(client);
            }
        }
    }
}

void TcpStreamer::ProcessClientRequestsIfAny() {
    std::lock_guard<std::mutex> lock(m_clientsLock);
    char buffer[1];
    for (int i = 0; i < m_clientCount; ++i) {
        int bytes = m_platform->Recv(m_clients[i].socket, buffer, 1, 0);
        if (bytes > 0 && static_cast<uint8_t>(buffer[0]) == PACKET_REQUEST_KEYFRAME) {
            setKeyframeRequested(true);
            LOGD(TAG, "Keyframe requested by client");
        }
    }
}

size_t TcpStreamer::FlushSendBuffers() {
    std::lock_guard<std::mutex> lock(m_clientsLock);
    size_t ret = (m_clientCount > 0) ? m_clients[0].sendBuffer.size() : 0;

    for (int i = 0; i < m_clientCount; ) {
        if (m_clients[i].sendBuffer.empty()) { i++; continue; }

        bool clientRemoved = false;
        while (!m_clients[i].sendBuffer.empty()) {
            char* ptr;
            size_t len;
            m_clients[i].sendBuffer.get_read_chunk(ptr, len);

            int bytesSent = m_platform->Send(m_clients[i].socket, ptr, static_cast<int>(len), 0);
            if (bytesSent == Platform::SEND_ERROR) {
                LOGE(TAG, "Send error on client " + std::to_string(i) + ", disconnecting");
                RemoveClient(i);
                clientRemoved = true;
                break;
            } else if (bytesSent > 0) {
                m_clients[i].sendBuffer.consume(bytesSent);
                if (bytesSent < static_cast<int>(len)) break;
            } else {
                break;
            }
        }
        if (!clientRemoved) i++;
    }
    return ret;
}

void TcpStreamer::QueuePacketToTcpClientSendBuffers(const std::vector<char>& packet, uint64_t now) {
    std::lock_guard<std::mutex> lock(m_clientsLock);
    for (int i = 0; i < m_clientCount; ) {
        if (m_clients[i].sendBuffer.size() > (size_t)m_config.maxClientBufferSize) {
            if (m_clients[i].bufferFullSince == 0)
                m_clients[i].bufferFullSince = now;
            else if (now - m_clients[i].bufferFullSince >= 5000) {
                LOGI(TAG, "Client " + std::to_string(i) + " send buffer full for 5s, disconnecting");
                RemoveClient(i);
                continue;
            }
            LOGI(TAG, "Client " + std::to_string(i) + " send buffer exceeded max size, dropping frame");
        } else {
            m_clients[i].bufferFullSince = 0;
            m_clients[i].sendBuffer.write(packet.data(), packet.size());
        }
        i++;
    }
}

void TcpStreamer::FlushLoop() {
    if (!m_platform->EnableMMCSSForCurrentThread()) {
        m_platform->SetCurrentThreadHighPriority();
    }
    while (m_running && !SignalHandler::isShutdownRequested()) {
        if (!HasClients()) { m_platform->Sleep(10); continue; }
        size_t bytesSent = FlushSendBuffers();
        m_totalBytesSent += bytesSent;
        if (bytesSent == 0) m_platform->Sleep(2);
    }
}

void* TcpStreamer::FlushThreadProc(void* param) {
    static_cast<TcpStreamer*>(param)->FlushLoop();
    return nullptr;
}
