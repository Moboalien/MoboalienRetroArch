#include "crypto/i_crypto_helper.h"
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <cstring>
#include <stdexcept>

/**
 * @class LinuxCryptoHelper
 * @brief OpenSSL-based implementation of ICryptoHelper.
 */
class LinuxCryptoHelper : public ICryptoHelper {
public:
    std::vector<uint8_t> GenerateRandomBytes(size_t length) override {
        std::vector<uint8_t> buffer(length);
        if (RAND_bytes(buffer.data(), static_cast<int>(length)) != 1) {
            throw std::runtime_error("Failed to generate random bytes");
        }
        return buffer;
    }

    RSAKeyPair GenerateRSAKeyPair() override {
        RSA* rsa = RSA_new();
        BIGNUM* bne = BN_new();
        if (!bne) {
            RSA_free(rsa);
            throw std::runtime_error("BN_new failed");
        }

        if (BN_set_word(bne, RSA_F4) != 1) {
            BN_free(bne);
            RSA_free(rsa);
            throw std::runtime_error("Failed to set RSA exponent");
        }

        if (RSA_generate_key_ex(rsa, 2048, bne, NULL) != 1) {
            BN_free(bne);
            RSA_free(rsa);
            throw std::runtime_error("Failed to generate RSA key");
        }
        BN_free(bne);

        RSAKeyPair keyPair;
        {
            BIO* bio_pub = BIO_new(BIO_s_mem());
            if (!PEM_write_bio_RSAPublicKey(bio_pub, rsa)) {
                BIO_free(bio_pub);
                RSA_free(rsa);
                throw std::runtime_error("Failed to write public key");
            }
            int pub_len = BIO_pending(bio_pub);
            keyPair.publicKey.resize(pub_len);
            BIO_read(bio_pub, keyPair.publicKey.data(), pub_len);
            BIO_free(bio_pub);
        }
        {
            BIO* bio_priv = BIO_new(BIO_s_mem());
            if (!PEM_write_bio_RSAPrivateKey(bio_priv, rsa, NULL, NULL, 0, NULL, NULL)) {
                BIO_free(bio_priv);
                RSA_free(rsa);
                throw std::runtime_error("Failed to write private key");
            }
            int priv_len = BIO_pending(bio_priv);
            keyPair.privateKey.resize(priv_len);
            BIO_read(bio_priv, keyPair.privateKey.data(), priv_len);
            BIO_free(bio_priv);
        }

        RSA_free(rsa);
        return keyPair;
    }

    std::vector<uint8_t> RSAEncrypt(const std::vector<uint8_t>& data, const std::vector<uint8_t>& publicKey) override {
        BIO* bio = BIO_new_mem_buf(publicKey.data(), static_cast<int>(publicKey.size()));
        RSA* rsa = PEM_read_bio_RSAPublicKey(bio, NULL, NULL, NULL);
        BIO_free(bio);
        if (!rsa) throw std::runtime_error("Failed to import public key");

        int key_len = RSA_size(rsa);
        std::vector<uint8_t> encrypted(key_len);
        int enc_len = RSA_public_encrypt(static_cast<int>(data.size()),
                                         reinterpret_cast<const unsigned char*>(data.data()),
                                         encrypted.data(), rsa, RSA_PKCS1_PADDING);
        RSA_free(rsa);
        if (enc_len == -1) throw std::runtime_error("RSA encryption failed");
        encrypted.resize(enc_len);
        return encrypted;
    }

    std::vector<uint8_t> RSADecrypt(const std::vector<uint8_t>& data, const std::vector<uint8_t>& privateKey) override {
        BIO* bio = BIO_new_mem_buf(privateKey.data(), static_cast<int>(privateKey.size()));
        RSA* rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL);
        BIO_free(bio);
        if (!rsa) throw std::runtime_error("Failed to import private key");

        int key_len = RSA_size(rsa);
        std::vector<uint8_t> decrypted(key_len);
        int dec_len = RSA_private_decrypt(static_cast<int>(data.size()),
                                          reinterpret_cast<const unsigned char*>(data.data()),
                                          decrypted.data(), rsa, RSA_PKCS1_PADDING);
        RSA_free(rsa);
        if (dec_len == -1) throw std::runtime_error("RSA decryption failed");
        decrypted.resize(dec_len);
        return decrypted;
    }

    std::string HashText(const std::string& password) override {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, reinterpret_cast<const unsigned char*>(password.data()), password.size());
        SHA256_Final(hash, &ctx);
        char buf[3];
        std::string out(SHA256_DIGEST_LENGTH * 2, ' ');
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            snprintf(buf, sizeof(buf), "%02x", hash[i]);
            out[i * 2] = buf[0];
            out[i * 2 + 1] = buf[1];
        }
        return out;
    }

    std::vector<uint8_t> GenerateAESKey() override {
        return GenerateRandomBytes(32);
    }

    void AESEncrypt(std::vector<uint8_t>& data, const std::vector<uint8_t>& key) override {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("Failed to create cipher context");

        unsigned char derived_key[32];
        unsigned char derived_iv[16];
        if (!EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha256(), NULL,
                            key.data(), static_cast<int>(key.size()), 1, derived_key, derived_iv)) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Key derivation failed");
        }

        if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, derived_key, derived_iv)) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to initialize encryption");
        }

        std::vector<uint8_t> out(data.size() + EVP_MAX_BLOCK_LENGTH);
        int out_len1 = 0, out_len2 = 0;
        if (!EVP_EncryptUpdate(ctx, out.data(), &out_len1, data.data(), static_cast<int>(data.size()))) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("AES encryption failed");
        }
        if (!EVP_EncryptFinal_ex(ctx, out.data() + out_len1, &out_len2)) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("AES finalization failed");
        }
        EVP_CIPHER_CTX_free(ctx);

        out.resize(out_len1 + out_len2);
        std::vector<uint8_t> result(derived_iv, derived_iv + 16);
        result.insert(result.end(), out.begin(), out.end());
        return result;
    }

    void AESDecrypt(std::vector<uint8_t>& encryptedData, const std::vector<uint8_t>& key) override {
        if (encryptedData.size() < 16) throw std::runtime_error("Encrypted data too small");
        unsigned char derived_key[32];
        unsigned char derived_iv[16];
        if (!EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha256(), NULL,
                            key.data(), static_cast<int>(key.size()), 1, derived_key, derived_iv)) {
            throw std::runtime_error("Key derivation failed");
        }

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("Failed to create cipher context");

        if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, derived_key, derived_iv)) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to initialize decryption");
        }

        std::vector<uint8_t> ciphertext(encryptedData.begin() + 16, encryptedData.end());
        std::vector<uint8_t> out(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
        int out_len1 = 0, out_len2 = 0;
        if (!EVP_DecryptUpdate(ctx, out.data(), &out_len1, ciphertext.data(), static_cast<int>(ciphertext.size()))) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("AES decryption failed");
        }
        if (!EVP_DecryptFinal_ex(ctx, out.data() + out_len1, &out_len2)) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("AES finalization failed");
        }
        EVP_CIPHER_CTX_free(ctx);

        out.resize(out_len1 + out_len2);
        return out;
    }
};

// Factory
ICryptoHelperPtr CreateCryptoHelper() {
    return std::make_shared<LinuxCryptoHelper>();
}
