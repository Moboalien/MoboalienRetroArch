#include "i_crypto_helper.h"
#include "logger.h"
#include "sha256.h"

#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <string>

// AES symbols are already compiled by griffin.c (deps/rcheevos/src/rhash/aes.c).
extern "C" {
#include "aes.h"
}

namespace moboalien {

static const char* TAG = "CryptoHelperRetroArch";

// CBC encrypt using only symbols available in the rcheevos trimmed aes.c.
// AES_CTR_xcrypt_buffer with a fresh context (Iv = block-sized counter starting
// at zero) produces keystream[0..15] = AES_ECB_encrypt(key, nonce).
// Setting nonce = zeros and input = plaintext XOR IV gives us CBC encrypt:
//   ciphertext = AES_ECB(key, plaintext XOR IV)
// which is exactly the CBC encrypt definition.
static void cbc_encrypt(const uint8_t* key, const uint8_t* iv,
                        uint8_t* buf, size_t length)
{
    uint8_t prev[AES_BLOCKLEN];
    memcpy(prev, iv, AES_BLOCKLEN);

    for (size_t i = 0; i < length; i += AES_BLOCKLEN) {
        // XOR plaintext block with previous ciphertext (or IV for first block)
        for (int j = 0; j < AES_BLOCKLEN; ++j)
            buf[i + j] ^= prev[j];

        // ECB-encrypt the block: use CTR with nonce = XORed block, input = zeros.
        // CTR keystream[0] = AES_ECB(key, nonce+0) = AES_ECB(key, nonce).
        // XOR zeros with keystream = keystream = AES_ECB(key, nonce).
        // But we want AES_ECB(key, buf[i..i+15]), so set nonce = buf[i..i+15]
        // and encrypt a zero block — result is AES_ECB(key, buf[i..i+15]).
        uint8_t zero[AES_BLOCKLEN] = {0};
        struct AES_ctx ecb_ctx;
        AES_init_ctx_iv(&ecb_ctx, key, buf + i);   // nonce = current block
        AES_CTR_xcrypt_buffer(&ecb_ctx, zero, AES_BLOCKLEN); // zero XOR keystream

        // zero now holds AES_ECB(key, buf[i..i+15]) — that's our ciphertext block
        memcpy(buf + i, zero, AES_BLOCKLEN);
        memcpy(prev, buf + i, AES_BLOCKLEN);
    }
}

class CryptoHelper : public ICryptoHelper {
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
        cbc_encrypt(key.data(), m_iv, data.data(), data.size());
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

} // namespace moboalien

ICryptoHelperPtr CreateCryptoHelper() {
    return std::make_shared<moboalien::CryptoHelper>();
}
