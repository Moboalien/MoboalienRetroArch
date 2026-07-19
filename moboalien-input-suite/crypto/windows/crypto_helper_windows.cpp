#include "i_crypto_helper.h"
#include <windows.h>
#include <wincrypt.h>
#include <cstring>
#include <stdexcept>
#include "aes.h"
#pragma comment(lib, "advapi32.lib")

/**
 * @class WindowsCryptoHelper
 * @brief Windows CryptoAPI-based implementation
 * 
 * Uses Windows' native CryptoAPI for all cryptographic operations.
 */
class WindowsCryptoHelper : public ICryptoHelper {
private:
    HCRYPTPROV hCryptProv;

    void InitCryptoProvider() {
        if (hCryptProv != 0) return;
        
        if (!CryptAcquireContextW(&hCryptProv, NULL, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            throw std::runtime_error("Failed to acquire crypto context");
        }
    }

public:
    WindowsCryptoHelper() : hCryptProv(0) {
        InitCryptoProvider();
    }

    ~WindowsCryptoHelper() {
        if (hCryptProv != 0) {
            CryptReleaseContext(hCryptProv, 0);
        }
    }

    std::vector<uint8_t> GenerateRandomBytes(size_t length) override {
        InitCryptoProvider();
        std::vector<uint8_t> buffer(length);
        
        if (!CryptGenRandom(hCryptProv, static_cast<DWORD>(length), buffer.data())) {
            throw std::runtime_error("Failed to generate random bytes");
        }
        
        return buffer;
    }

    RSAKeyPair GenerateRSAKeyPair() override {
        InitCryptoProvider();
        
        HCRYPTKEY hKey = 0;
        RSAKeyPair keyPair;
        
        try {
            if (!CryptGenKey(hCryptProv, AT_KEYEXCHANGE, (2048 << 16) | CRYPT_EXPORTABLE, &hKey)) {
                throw std::runtime_error("Failed to generate RSA key");
            }
            
            DWORD publicKeySize = 0;
            if (!CryptExportKey(hKey, 0, PUBLICKEYBLOB, 0, NULL, &publicKeySize)) {
                throw std::runtime_error("Failed to get public key size");
            }
            
            keyPair.publicKey.resize(publicKeySize);
            if (!CryptExportKey(hKey, 0, PUBLICKEYBLOB, 0, keyPair.publicKey.data(), &publicKeySize)) {
                throw std::runtime_error("Failed to export public key");
            }
            
            DWORD privateKeySize = 0;
            if (!CryptExportKey(hKey, 0, PRIVATEKEYBLOB, 0, NULL, &privateKeySize)) {
                throw std::runtime_error("Failed to get private key size");
            }
            
            keyPair.privateKey.resize(privateKeySize);
            if (!CryptExportKey(hKey, 0, PRIVATEKEYBLOB, 0, keyPair.privateKey.data(), &privateKeySize)) {
                throw std::runtime_error("Failed to export private key");
            }
            
            CryptDestroyKey(hKey);
            return keyPair;
        }
        catch (...) {
            if (hKey != 0) CryptDestroyKey(hKey);
            throw;
        }
    }

    std::vector<uint8_t> RSAEncrypt(const std::vector<uint8_t>& data, const std::vector<uint8_t>& publicKey) override {
        InitCryptoProvider();
        
        HCRYPTKEY hKey = 0;
        
        try {
            if (!CryptImportKey(hCryptProv, publicKey.data(), static_cast<DWORD>(publicKey.size()), 
                               0, 0, &hKey)) {
                throw std::runtime_error("Failed to import public key");
            }
            
            std::vector<uint8_t> encryptedData = data;
            DWORD dataLength = static_cast<DWORD>(data.size());
            DWORD bufferLength = dataLength + 128;
            
            encryptedData.resize(bufferLength);
            
            if (!CryptEncrypt(hKey, 0, TRUE, 0, encryptedData.data(), &dataLength, bufferLength)) {
                throw std::runtime_error("RSA encryption failed");
            }
            
            encryptedData.resize(dataLength);
            CryptDestroyKey(hKey);
            return encryptedData;
        }
        catch (...) {
            if (hKey != 0) CryptDestroyKey(hKey);
            throw;
        }
    }

    std::vector<uint8_t> RSADecrypt(const std::vector<uint8_t>& data, const std::vector<uint8_t>& privateKey) override {
        InitCryptoProvider();
        
        HCRYPTKEY hKey = 0;
        
        try {
            if (!CryptImportKey(hCryptProv, privateKey.data(), static_cast<DWORD>(privateKey.size()), 
                               0, 0, &hKey)) {
                throw std::runtime_error("Failed to import private key");
            }
            
            std::vector<uint8_t> decryptedData = data;
            DWORD dataLength = static_cast<DWORD>(data.size());
            
            if (!CryptDecrypt(hKey, 0, TRUE, 0, decryptedData.data(), &dataLength)) {
                throw std::runtime_error("RSA decryption failed");
            }
            
            decryptedData.resize(dataLength);
            CryptDestroyKey(hKey);
            return decryptedData;
        }
        catch (...) {
            if (hKey != 0) CryptDestroyKey(hKey);
            throw;
        }
    }

    std::string HashText(const std::string& password) override {
        InitCryptoProvider();
        
        HCRYPTHASH hHash = 0;
        
        try {
            if (!CryptCreateHash(hCryptProv, CALG_SHA_256, 0, 0, &hHash)) {
                throw std::runtime_error("Failed to create hash object");
            }
            
            if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(password.c_str()), 
                              static_cast<DWORD>(password.size()), 0)) {
                throw std::runtime_error("Failed to hash data");
            }
            
            BYTE hashValue[32];
            DWORD hashSize = 32;
            
            if (!CryptGetHashParam(hHash, 2, hashValue, &hashSize, 0)) {
                throw std::runtime_error("Failed to get hash value");
            }
            
            std::string result;
            for (DWORD i = 0; i < hashSize; ++i) {
                char buf[3];
                snprintf(buf, sizeof(buf), "%02x", hashValue[i]);
                result += buf;
            }
            
            CryptDestroyHash(hHash);
            return result;
        }
        catch (...) {
            if (hHash != 0) CryptDestroyHash(hHash);
            throw;
        }
    }

    std::vector<uint8_t> GenerateAESKey() override {
        return GenerateRandomBytes(32);
    }

    void apply_pkcs7_padding(std::vector<uint8_t>& data) {
        uint8_t padding_len = 16 - (data.size() % 16);
        for (int i = 0; i < padding_len; ++i) {
            data.push_back(padding_len);
        }
    }

    void remove_pkcs7_padding(std::vector<uint8_t>& data) {
        if (data.empty()) return;
        uint8_t pad_len = data.back();
        // Validate padding (every pad byte must equal pad_len)
        if (pad_len > 0 && pad_len <= 16) {
            data.resize(data.size() - pad_len);
        }
    }
    uint8_t iv[16] = {0}; 
    void AESEncrypt(std::vector<uint8_t>& data, const std::vector<uint8_t>& key) override {
        apply_pkcs7_padding(data); // Step 1: Pad to 16-byte blocks
        struct AES_ctx ctx;
        AES_init_ctx_iv(&ctx, key.data(), iv);
        
        // Step 2: Encrypt in-place (size is now a multiple of 16)
        AES_CBC_encrypt_buffer(&ctx, data.data(), data.size());
    }

    void AESDecrypt(std::vector<uint8_t>& encryptedData, const std::vector<uint8_t>& key) override {
        struct AES_ctx ctx;
        AES_init_ctx_iv(&ctx, key.data(), iv);
    
        // 3. Decrypt in-place (encrypted_data size must be multiple of 16)
        AES_CBC_decrypt_buffer(&ctx, encryptedData.data(), encryptedData.size());
        
        // 4. Manually remove the PKCS7 padding Java added
        remove_pkcs7_padding(encryptedData);
    }
};

// Factory implementation
ICryptoHelperPtr CreateCryptoHelper() {
    return std::make_shared<WindowsCryptoHelper>();
}
