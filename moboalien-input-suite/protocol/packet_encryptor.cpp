#include "packet_encryptor.h"
#include "utils.h"
#include <cstring>

static const char* TAG = "PacketEncryptor";

PacketEncryptor::PacketEncryptor(std::shared_ptr<ICryptoHelper> cryptoHelper, Platform* platform, uint64_t serverTicks, uint64_t clientTicks, const std::vector<uint8_t>& encryptionKey) 
    : m_cryptoHelper(cryptoHelper), m_platform(platform), m_encryptionKey(encryptionKey) {
    // Calculate time offset: serverTime - clientTime
    m_timeOffset = static_cast<int64_t>(serverTicks) - static_cast<int64_t>(clientTicks);
    LOGI(TAG, "Constructor: timeOffset=" + std::to_string(m_timeOffset));
}

uint64_t PacketEncryptor::GetCurrentTime() const {
    return m_platform->GetTickCountMs();
}

std::vector<uint8_t> PacketEncryptor::EncryptPacket(std::vector<uint8_t>& data) {
    // Safety checks
    if (!m_cryptoHelper) {
        LOGE(TAG, "m_cryptoHelper is null!");
        return std::vector<uint8_t>();
    }
    
    if (!m_platform) {
        LOGE(TAG, "m_platform is null!");
        return std::vector<uint8_t>();
    }
    
    if (m_encryptionKey.empty()) {
        LOGE(TAG, "m_encryptionKey is empty!");
        return std::vector<uint8_t>();
    }
    
    uint64_t currentTime = GetCurrentTime();
    auto dataWithTimestamp = PackWithTimestamp(data, currentTime);
    m_cryptoHelper->AESEncrypt(dataWithTimestamp, m_encryptionKey);
    
    // Check if encrypted size is reasonable (should be multiple of 16 for AES)
    if (dataWithTimestamp.size() % 16 != 0) {
        LOGE(TAG, "Encrypted size " + std::to_string(data.size()) + " is not a multiple of 16!");
    }
    
    return dataWithTimestamp;
}

std::vector<uint8_t> PacketEncryptor::DecryptPacket(std::vector<uint8_t>& data, bool& outIsValid) {
    outIsValid = false;
    
    // Safety checks
    if (!m_cryptoHelper) {
        LOGE(TAG, "DecryptPacket: m_cryptoHelper is null!");
        return std::vector<uint8_t>();
    }
    
    if (!m_platform) {
        LOGE(TAG, "DecryptPacket: m_platform is null!");
        return std::vector<uint8_t>();
    }
    
    if (m_encryptionKey.empty()) {
        LOGE(TAG, "DecryptPacket: m_encryptionKey is empty!");
        return std::vector<uint8_t>();
    }
    
    // Decrypt the data
    m_cryptoHelper->AESDecrypt(data, m_encryptionKey);
    
    if (data.empty()) {
        LOGE(TAG, "DecryptPacket: AESDecrypt returned empty data!");
        return std::vector<uint8_t>();
    }
    
    // Extract timestamp and data
    uint64_t packetTimestamp;
    auto packetData = UnpackTimestamp(data, packetTimestamp);
    if (packetData.empty()) {
        LOGE(TAG, "DecryptPacket: UnpackTimestamp returned empty data!");
        return std::vector<uint8_t>();
    }
    
    // Validate timestamp (packet must be sent within 5 seconds)
    uint64_t currentTime = GetCurrentTime();
    uint64_t timeDiff = (currentTime > packetTimestamp) ? (currentTime - packetTimestamp) : (packetTimestamp - currentTime);
    
    if (timeDiff > 5000) { // 5 seconds in milliseconds
        LOGE(TAG, "DecryptPacket: Packet too old, timeDiff=" + std::to_string(timeDiff) + "ms");
        return std::vector<uint8_t>(); // Packet too old
    }
    
    outIsValid = true;
    return packetData;
}

std::vector<uint8_t> PacketEncryptor::PackWithTimestamp(const std::vector<uint8_t>& data, uint64_t timestamp) {
    std::vector<uint8_t> packed;
    AppendInt64(packed, static_cast<int64_t>(timestamp));
    
    // Add original data
    packed.insert(packed.end(), data.begin(), data.end());
    
    return packed;
}

std::vector<uint8_t> PacketEncryptor::UnpackTimestamp(const std::vector<uint8_t>& data, uint64_t& outTimestamp) {
    if (data.size() < sizeof(uint64_t)) {
        return std::vector<uint8_t>();
    }
    outTimestamp = static_cast<uint64_t>(ReadInt64(reinterpret_cast<const char*>(data.data())));
    // Return remaining data (skip timestamp)
    return std::vector<uint8_t>(data.begin() + sizeof(uint64_t), data.end());
}
