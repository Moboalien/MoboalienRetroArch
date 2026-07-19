#pragma once

#include <array>
#include <cstdint>
#include <string>

// Computes SHA-256 digest of input string and returns 32 byte array.
std::array<uint8_t, 32> Sha256(const std::string& input);
