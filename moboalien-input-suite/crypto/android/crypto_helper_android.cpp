#include "i_crypto_helper.h"
#include "logger.h"
#include "sha256.h"

#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <string>

// Define AES_STATIC=static before including aes.c so every public AES symbol
// gets internal linkage in this translation unit. This prevents duplicate
// symbol errors with the rcheevos copy already compiled by griffin.c.
#define AES_STATIC static
extern "C" {
#include "aes.h"
#include "../aes.c"
}

static const char* TAG = "CryptoHelperAndroid";

class AndroidCryptoHelper : public ICryptoHelper {
public:
    std::vector<uint8_t> GenerateRandomBytes(size_t length) override {
        std::vector<uint8_t> buf(length);
        arc4random_buf(buf.data(), length);
        return buf;
    }

    std::string HashText(const std::string& text) override {
        auto digest = Sha256(text);
        char hex[65];
        for (int i = 0; i < 32; ++i)
            snprintf(hex + i * 2, 3, "%02x", digest[i]);
        return std::string(hex, 64);
    }

    std::vector<uint8_t> GenerateAESKey() override {
        return GenerateRandomBytes(32);
    }

    // AES-128-CBC, zero IV, PKCS7 padding — matches WindowsCryptoHelper exactly
    void AESEncrypt(std::vector<uint8_t>& data, const std::vector<uint8_t>& key) override {
        apply_pkcs7_padding(data);
        struct AES_ctx ctx;
        AES_init_ctx_iv(&ctx, key.data(), m_iv);
        AES_CBC_encrypt_buffer(&ctx, data.data(), data.size());
    }

    void AESDecrypt(std::vector<uint8_t>& data, const std::vector<uint8_t>& key) override {
        if (data.size() % AES_BLOCKLEN != 0)
            throw std::runtime_error("AESDecrypt: data not block-aligned");
        struct AES_ctx ctx;
        AES_init_ctx_iv(&ctx, key.data(), m_iv);
        AES_CBC_decrypt_buffer(&ctx, data.data(), data.size());
        remove_pkcs7_padding(data);
    }

    RSAKeyPair GenerateRSAKeyPair()                                                           override { return {}; }
    std::vector<uint8_t> RSAEncrypt(const std::vector<uint8_t>&, const std::vector<uint8_t>&) override { return {}; }
    std::vector<uint8_t> RSADecrypt(const std::vector<uint8_t>&, const std::vector<uint8_t>&) override { return {}; }

private:
    uint8_t m_iv[AES_BLOCKLEN] = {0};

    static void apply_pkcs7_padding(std::vector<uint8_t>& data) {
        uint8_t pad = static_cast<uint8_t>(AES_BLOCKLEN - (data.size() % AES_BLOCKLEN));
        for (uint8_t i = 0; i < pad; ++i)
            data.push_back(pad);
    }

    static void remove_pkcs7_padding(std::vector<uint8_t>& data) {
        if (data.empty()) return;
        uint8_t pad = data.back();
        if (pad > 0 && pad <= AES_BLOCKLEN)
            data.resize(data.size() - pad);
    }
};

ICryptoHelperPtr CreateCryptoHelper() {
    return std::make_shared<AndroidCryptoHelper>();
}
