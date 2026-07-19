#include "handshake_client.h"
#include "packet_codec.h"
#include "utils.h"
#include "i_crypto_helper.h"
#include <iostream>

namespace Controller {

HandshakeClient::HandshakeClient(Platform* platform)
    : platform_(platform), m_clientTicks(platform->GetTickCountMs()) {
}

HandshakeResult HandshakeClient::PerformHandshake(const std::string& serverIP, int handshakePort, const std::string& password) {
    // Create UDP socket
    uintptr_t sock = platform_->CreateUDPSocket(0);
    if (sock == Platform::INVALID_SOCKET_HANDLE) {
        return HandshakeResult(false, "Socket creation failed");
    }

    // Perform handshake steps
    if (!SendHelloPacket(sock, serverIP, handshakePort)) {
        platform_->CloseSocket(sock);
        return HandshakeResult(false, "Failed to send Hello packet");
    }

    std::vector<uint8_t> salt;
    uint16_t controllerPort, screenPort;
    if (!ReceiveHelloResponse(sock, salt, controllerPort, screenPort)) {
        platform_->CloseSocket(sock);
        return HandshakeResult(false, "Failed to receive Hello response");
    }

    if (!SendAuthPacket(sock, serverIP, handshakePort, password, salt)) {
        platform_->CloseSocket(sock);
        return HandshakeResult(false, "Failed to send Auth packet");
    }

    uint64_t serverTicks;
    if (!ReceiveAuthResult(sock, serverTicks)) {
        platform_->CloseSocket(sock);
        return HandshakeResult(false, "Authentication failed");
    }

    platform_->CloseSocket(sock);
    
    auto cryptoHelper = CreateCryptoHelper();
    // Hash the password first
    std::string passwordHash = cryptoHelper->HashText(password);
    std::cout<<"Password Hash: "<<passwordHash<<std::endl;
    std::cout<<"Salt (hex): "<<BytesToHex(salt)<<std::endl;
    // Concatenate password hash (hex string) bytes with raw salt bytes
    std::vector<uint8_t> passHashBytes(passwordHash.begin(), passwordHash.end());
    std::vector<uint8_t> combined = passHashBytes;
    combined.insert(combined.end(), salt.begin(), salt.end());
    // Hash the combined bytes
    std::string combinedStr(combined.begin(), combined.end());
    std::string keyhash = cryptoHelper->HashText(combinedStr);
    std::vector<uint8_t> encryptionKey = *HexToBytes(keyhash);
    encryptionKey.resize(16); // Use 128-bit key for Java compatibility
    m_packetEncryptor = std::make_shared<PacketEncryptor>(
        cryptoHelper,  // Pass the shared_ptr directly
        platform_,
        serverTicks,
        m_clientTicks,
        encryptionKey
    );
    
    HandshakeResult result(true);
    result.salt = salt;
    result.controllerPort = controllerPort;
    result.screenPort = screenPort;
    result.serverTicks = serverTicks;
    return result;
}

bool HandshakeClient::SendHelloPacket(uintptr_t sock, const std::string& serverIP, int handshakePort) {
    auto helloPacket = CreateHelloPacket(1, 0);
    int sent = platform_->SendTo(sock, helloPacket.data(), helloPacket.size(), 0, serverIP, handshakePort);
    return sent > 0;
}

bool HandshakeClient::ReceiveHelloResponse(uintptr_t sock, std::vector<uint8_t>& outSalt, uint16_t& outControllerPort, uint16_t& outScreenPort) {
    char buffer[1024];
    std::string senderIP;
    int senderPort;
    int len = platform_->RecvFrom(sock, buffer, sizeof(buffer), 0, senderIP, senderPort);
    
    if (len <= 0 || static_cast<uint8_t>(buffer[0]) != PACKET_HELLO_RESPONSE) {
        return false;
    }
    
    HelloResponsePacket helloResp;
    if (!ParseHelloResponsePacket(buffer + 1, len - 1, helloResp, outSalt)) {
        return false;
    }
    
    outControllerPort = helloResp.controllerPort;
    outScreenPort = helloResp.screenPort;
    return true;
}

bool HandshakeClient::SendAuthPacket(uintptr_t sock, const std::string& serverIP, int handshakePort, 
                                     const std::string& password, const std::vector<uint8_t>& salt) {
    auto crypto = CreateCryptoHelper();
    std::string passwordHash = crypto->HashText(password);
    std::string saltStr(salt.begin(), salt.end());
    std::string sessionKeyHash = crypto->HashText(passwordHash + saltStr);
    
    // Create a random challenge
    std::string challenge = "Challenge123456"; // In real app, use random bytes
    std::string payload = "MAGIC" + challenge;
    
    // Encrypt challenge with session key
    auto key = HexToBytes(sessionKeyHash);
    if (!key) {
        return false;
    }
    key->resize(16); // Use 128-bit key for Java compatibility
    
    std::vector<uint8_t> encrypted = std::vector<uint8_t>(payload.begin(), payload.end());
    crypto->AESEncrypt(encrypted, *key);
    auto authPacket = CreateAuthPacket(encrypted);
    
    int sent = platform_->SendTo(sock, authPacket.data(), authPacket.size(), 0, serverIP, handshakePort);
    return sent > 0;
}

bool HandshakeClient::ReceiveAuthResult(uintptr_t sock, uint64_t& outServerTicks) {
    char buffer[1024];
    std::string senderIP;
    int senderPort;
    int len = platform_->RecvFrom(sock, buffer, sizeof(buffer), 0, senderIP, senderPort);
    
    if (len <= 0 || static_cast<uint8_t>(buffer[0]) != PACKET_AUTH_RESULT) {
        return false;
    }
    
    AuthResultPacket authResult;
    if (!ParseAuthResultPacket(buffer + 1, len - 1, authResult)) {
        return false;
    }
    
    outServerTicks = authResult.serverTicks;
    return authResult.result == 1;
}

} // namespace Controller
