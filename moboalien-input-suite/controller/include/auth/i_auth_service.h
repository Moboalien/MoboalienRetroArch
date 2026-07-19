#pragma once

#include <vector>
#include <cstdint>

class IAuthService {
public:
    virtual ~IAuthService() = default;

    // Validate an encrypted auth payload using the provided session key.
    // Returns true when authentication succeeds.
    virtual bool ValidateAuth(std::vector<uint8_t>& encryptedData, const std::vector<uint8_t>& sessionKey) = 0;
};
