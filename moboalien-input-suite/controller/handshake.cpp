#include "controller/handshake.h"
#include "packet_codec.h"
#include "packet_types.h"
#include "signal_handler.h"
#include "utils.h"
#include <chrono>
#include <thread>

static const char* TAG = "HandshakeManager";

HandshakeManager::~HandshakeManager() {
    Stop();
}

void HandshakeManager::Start() {
    if (m_running.exchange(true)) return; // already running
    
    // Initialize sockets for this thread
    if (!m_platform->SocketsInitialize()) {
        LOGE(TAG, "Handshake socket initialization failed");
        m_running = false;
        return;
    }
    
    // Register cleanup callback with signal handler
    if (m_shutdownCallbackId == 0) {
        m_shutdownCallbackId = SignalHandler::getInstance().registerCallback([this]() {
            LOGI(TAG, "Handshake manager shutdown callback triggered");
            this->Stop();
        });
    }
    
    m_thread = std::thread(&HandshakeManager::ThreadLoop, this);
}

void HandshakeManager::Stop() {
    if (!m_running.exchange(false)) return; // not running

    // Closing the socket will cause RecvFrom to fail/return so the thread can exit.
    if (m_socket != Platform::INVALID_SOCKET_HANDLE && m_platform) {
        m_platform->CloseSocket(m_socket);
    }

    if (m_thread.joinable()) m_thread.join();

    // Unregister shutdown callback
    if (m_shutdownCallbackId != 0) {
        SignalHandler::getInstance().unregisterCallback(m_shutdownCallbackId);
        m_shutdownCallbackId = 0;
    }
}

void HandshakeManager::ThreadLoop() {
    if (!m_platform) return;

    uintptr_t sock = m_platform->CreateUDPSocket(static_cast<int>(m_handshakePort));
    if (sock == Platform::INVALID_SOCKET_HANDLE) {
        LOGE(TAG, "Failed to create handshake socket on port " + std::to_string(m_handshakePort));
        m_running = false;
        return;
    }

    m_socket = sock;
    LOGI(TAG, "Handshake server listening on port " + std::to_string(m_handshakePort));

    char buffer[1024];
    while (m_running && !SignalHandler::isShutdownRequested()) {
        std::string clientIP;
        int clientPort = 0;
        int len = m_platform->RecvFrom(sock, buffer, sizeof(buffer), 0, clientIP, clientPort);
        if (len > 0) {
            uint8_t type = static_cast<uint8_t>(buffer[0]);
            std::string clientKey = clientIP + ":" + std::to_string(clientPort);
            LOGD(TAG, "Received packet of type " + std::to_string((int)type) + " from " + clientKey);
            if (type == PACKET_HELLO) {
                HelloPacket hello;
                if (Controller::ParseHelloPacket(buffer + 1, len - 1, hello)) {
                    // Convert salt string to bytes (UTF-8 encoding)
                    // This matches Java UTF-8 encoding for consistent key derivation
                    std::vector<uint8_t> saltBytes(m_salt.begin(), m_salt.end());
                    auto resp = Controller::CreateHelloResponsePacket(1, PROTOCOL_VERSION, m_controllerPort, m_screenPort, m_serverId, m_serverType, m_passwordRequired, saltBytes);
                    m_platform->SendTo(sock, resp.data(), resp.size(), 0, clientIP, clientPort);
                }
            } else if (type == PACKET_AUTH) {
                AuthPacket auth;
                std::vector<uint8_t> encrypted;
                if (Controller::ParseAuthPacket(buffer + 1, len - 1, auth, encrypted)) {
                    bool success = true;
                    if (m_auth) {
                        success = m_auth->ValidateAuth(encrypted, m_sessionKey);
                    }
                    auto result = Controller::CreateAuthResultPacket(success, m_platform->GetTickCountMs());
                    m_platform->SendTo(sock, result.data(), result.size(), 0, clientIP, clientPort);
                    if (success) LOGI(TAG, "Client authenticated: " + clientKey);
                }
                else
                {
                    LOGW(TAG, "Client authentication failed: " + clientKey);
                }
            }
        } else {
            // If RecvFrom returned <= 0, either socket was closed or error occurred.
            // Sleep briefly to avoid busy-looping when not running.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    if (m_socket != Platform::INVALID_SOCKET_HANDLE) {
        m_platform->CloseSocket(m_socket);
        m_socket = Platform::INVALID_SOCKET_HANDLE;
    }
    
    LOGI(TAG, "Handshake server stopped");
}
