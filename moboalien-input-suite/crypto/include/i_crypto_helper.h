#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

/**
 * @interface IAuthService
 * @brief Platform-independent interface for cryptographic authentication operations.
 * 
 * Provides RSA key pair generation, RSA encryption/decryption, SHA256 hashing,
 * and AES-256 encryption/decryption for secure authentication flows.
 */
class ICryptoHelper {
public:
    virtual ~ICryptoHelper() = default;

    struct RSAKeyPair {
        std::vector<uint8_t> publicKey;
        std::vector<uint8_t> privateKey;
    };

    struct AuthChallenge {
        std::vector<uint8_t> challenge;
        std::vector<uint8_t> publicKey;
    };

    struct AuthResponse {
        std::vector<uint8_t> encryptedResponse;
    };

    // RSA Operations
    virtual RSAKeyPair GenerateRSAKeyPair() = 0;
    virtual std::vector<uint8_t> RSAEncrypt(const std::vector<uint8_t>& data, const std::vector<uint8_t>& publicKey) = 0;
    virtual std::vector<uint8_t> RSADecrypt(const std::vector<uint8_t>& data, const std::vector<uint8_t>& privateKey) = 0;

    // Hashing
    virtual std::string HashText(const std::string& password) = 0;

    // AES Operations
    virtual std::vector<uint8_t> GenerateAESKey() = 0;
    virtual void AESEncrypt(std::vector<uint8_t>& data, const std::vector<uint8_t>& key) = 0;
    virtual void AESDecrypt(std::vector<uint8_t>& encryptedData, const std::vector<uint8_t>& key) = 0;
    // Random byte generation
    virtual std::vector<uint8_t> GenerateRandomBytes(size_t length) = 0;
};

// Smart pointer type for dependency injection
using ICryptoHelperPtr = std::shared_ptr<ICryptoHelper>;

/**
 * Factory function to create the platform-specific authentication service.
 * @return Shared pointer to a platform-appropriate IAuthService implementation.
 */
ICryptoHelperPtr CreateCryptoHelper();
