#include "auth_service.h"
#include <string>

AuthService::AuthService(ICryptoHelper* crypto) : m_crypto(crypto) {}

bool AuthService::ValidateAuth(std::vector<uint8_t>& data, const std::vector<uint8_t>& sessionKey) {
    if (!m_crypto) return false;
    
    try {
        m_crypto->AESDecrypt(data, sessionKey);
        std::string decryptedStr(data.begin(), data.end());
        // Simple validation: payload must start with "MAGIC"
        return decryptedStr.find("MAGIC") == 0;
    } catch (const std::exception& e) {
        // Decryption failed - likely wrong password or corrupted data
        return false;
    } catch (...) {
        // Unknown error during decryption
        return false;
    }
}

std::unique_ptr<IAuthService> CreateAuthService(ICryptoHelper* crypto) {
    return std::make_unique<AuthService>(crypto);
}
