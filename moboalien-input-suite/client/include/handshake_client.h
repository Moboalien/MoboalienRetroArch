#pragma once

#include <string>
#include <vector>
#include <memory>
#include "packet_types.h"
#include "platform.h"
#include "packet_encryptor.h"

namespace Controller {

struct HandshakeResult {
    bool success;
    std::string errorMessage;
    std::vector<uint8_t> salt;
    uint16_t controllerPort;
    uint16_t screenPort;
    uint64_t serverTicks;
    
    HandshakeResult(bool s = false, const std::string& msg = "") 
        : success(s), errorMessage(msg), controllerPort(0), screenPort(0), serverTicks(0) {}
};

class HandshakeClient {
public:
    HandshakeClient(Platform* platform);
    ~HandshakeClient() = default;

    // Perform complete handshake with server
    HandshakeResult PerformHandshake(const std::string& serverIP, int handshakePort, const std::string& password);
    
    // Get the packet encryptor (created after successful handshake)
    std::shared_ptr<PacketEncryptor> GetPacketEncryptor() const { return m_packetEncryptor; }

private:
    Platform* platform_;
    std::shared_ptr<PacketEncryptor> m_packetEncryptor;
    uint64_t m_clientTicks;
    
    // Individual handshake steps
    bool SendHelloPacket(uintptr_t sock, const std::string& serverIP, int handshakePort);
    bool ReceiveHelloResponse(uintptr_t sock, std::vector<uint8_t>& outSalt, uint16_t& outControllerPort, uint16_t& outScreenPort);
    bool SendAuthPacket(uintptr_t sock, const std::string& serverIP, int handshakePort, 
                        const std::string& password, const std::vector<uint8_t>& salt);
    bool ReceiveAuthResult(uintptr_t sock, uint64_t& outServerTicks);
};

} // namespace Controller
