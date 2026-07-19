#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include "platform.h"
#include "auth/i_auth_service.h"

class HandshakeManager {
public:
    // handshakePort: UDP port to listen on for handshake packets (e.g., 16235)
    HandshakeManager(Platform* platform, IAuthService* authService, const std::vector<uint8_t>& sessionKey, const std::string& salt, uint16_t handshakePort, uint16_t controllerPort, uint16_t screenPort, uint64_t serverId, uint16_t serverType, bool passwordRequired)
        : m_platform(platform), m_auth(authService), m_sessionKey(sessionKey), m_salt(salt), m_handshakePort(handshakePort), m_controllerPort(controllerPort), m_screenPort(screenPort), m_serverId(serverId), m_serverType(serverType), m_passwordRequired(passwordRequired), m_running(false)  {};
    ~HandshakeManager();

    void Start();
    void Stop();

private:
    void ThreadLoop();

    Platform* m_platform;
    IAuthService* m_auth;
    std::vector<uint8_t> m_sessionKey;
    std::string m_salt;
    uint16_t m_handshakePort;
    uint16_t m_controllerPort;
    uint16_t m_screenPort;

    std::atomic<bool> m_running;
    std::thread m_thread;
    uintptr_t m_socket;
    uint64_t m_serverId;
    uint16_t m_serverType;
    bool m_passwordRequired;
    // Registered shutdown callback id (0 = none)
    uint64_t m_shutdownCallbackId = 0;
};
