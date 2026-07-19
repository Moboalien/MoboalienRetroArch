#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include "i_crypto_helper.h"
#include "platform.h"

class 
PacketEncryptor {
public:
    PacketEncryptor(std::shared_ptr<ICryptoHelper> cryptoHelper, Platform* platform, uint64_t serverTicks, uint64_t clientTicks, const std::vector<uint8_t>& encryptionKey);
    ~PacketEncryptor() = default;

    // Encrypt packet with timestamp
    std::vector<uint8_t> EncryptPacket(std::vector<uint8_t>& packetData);
    
    // Decrypt packet and validate timestamp (returns decrypted data)
    std::vector<uint8_t> DecryptPacket(std::vector<uint8_t>& encryptedData, bool& outIsValid);
    
    // Get time offset for debugging
    int64_t GetTimeOffset() const { return m_timeOffset; }

private:
    std::shared_ptr<ICryptoHelper> m_cryptoHelper;
    Platform* m_platform;
    std::vector<uint8_t> m_encryptionKey;
    int64_t m_timeOffset; // serverTime - clientTime
    
    // Track recent packets to prevent duplicates within 5 seconds
    std::unordered_map<uint64_t, uint64_t> m_recentPackets; // hash -> timestamp
    
    // Get current time in milliseconds
    uint64_t GetCurrentTime() const;
    
    // Pack/unpack timestamp with data
    std::vector<uint8_t> PackWithTimestamp(const std::vector<uint8_t>& data, uint64_t timestamp);
    std::vector<uint8_t> UnpackTimestamp(const std::vector<uint8_t>& data, uint64_t& outTimestamp);
    
    // Calculate simple hash of packet data for duplicate detection
    uint64_t CalculatePacketHash(const std::vector<uint8_t>& data) const;
    
    // Clean up old packet entries (older than 5 seconds)
    void CleanupOldPackets(uint64_t currentTime);
};
