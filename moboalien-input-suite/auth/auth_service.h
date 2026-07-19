#pragma once

#include "auth/i_auth_service.h"
#include "i_crypto_helper.h"
#include <memory>

std::unique_ptr<IAuthService> CreateAuthService(ICryptoHelper* crypto);

class AuthService : public IAuthService {
public:
    explicit AuthService(ICryptoHelper* crypto);
    bool ValidateAuth(std::vector<uint8_t>& encryptedData, const std::vector<uint8_t>& sessionKey) override;

private:
    ICryptoHelper* m_crypto;
};
