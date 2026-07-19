#pragma once
#include <vector>
#include <cstring> // For std::memcmp
#ifndef __ARM_ARCH
#include <immintrin.h> // for AVX2 intrinsics (x86/x64 only)
#endif
#include <cstdint> // For int32_t
#include <cstddef> // For size_t
#include <string>
#include <optional>
#include "logger.h"

// Dumps raw data to a file.
// This function is for debugging purposes.
void DumpDataToFile(const char* filename, const void* data, size_t size);

// Appends a 32-bit integer to a vector in network byte order (big-endian).
void AppendInt32(std::vector<char>& vec, int32_t value);

// Reads a 32-bit integer from a buffer in network byte order (big-endian).
// The buffer must contain at least 4 bytes.
int32_t ReadInt32(const char* buffer);

// Appends a 64-bit integer to a vector in network byte order (big-endian).
void AppendInt64(std::vector<char>& vec, int64_t value);
void AppendInt64(std::vector<uint8_t>& vec, int64_t value);

// Reads a 64-bit integer from a buffer in network byte order (big-endian).
int64_t ReadInt64(const char* buffer);

// Appends a 32-bit float to a vector in network byte order (big-endian).
void AppendFloat(std::vector<char>& vec, float value);
void AppendFloat(std::vector<uint8_t>& vec, float value);

// Reads a 32-bit float from a buffer in network byte order (big-endian).
float ReadFloat(const char* buffer);

// Convert a buffer of bytes to a lowercase hex string.
std::string BytesToHex(const uint8_t* data, size_t len);
std::string BytesToHex(const std::vector<uint8_t>& data);

// Parse a hex string (even length) into bytes. Returns nullopt on invalid input.
std::optional<std::vector<uint8_t>> HexToBytes(const std::string& hex);

uint32_t HashData(const unsigned char *data, uint32_t size, uint32_t sampleStep = 1);

int16_t ReadInt16(const char* buffer);

void AppendInt16(std::vector<char>& vec, int16_t value);
void AppendInt16(std::vector<uint8_t>& vec, int16_t value);

// Generates a random alphanumeric string of the specified length.
std::string GenerateRandomSalt(size_t length);

inline int memcmp_SIMD(const void* pNew, const void* pPrev, size_t numBytes)
{
#if defined(__AVX2__)
    const size_t simdWidth = 32; // 256 bits = 32 bytes
    size_t i = 0;
    const auto* p1 = static_cast<const unsigned char*>(pNew);
    const auto* p2 = static_cast<const unsigned char*>(pPrev);

    for (; i + simdWidth <= numBytes; i += simdWidth) {
        const __m256i vNew  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p1 + i));
        const __m256i vPrev = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p2 + i));
        // XOR the two vectors. If they are identical, the result is all zeros.
        const __m256i diff = _mm256_xor_si256(vNew, vPrev);
        // _mm256_testz_si256(a, a) returns 1 if 'a' is all zeros.
        // If it's NOT all zeros, we have a difference.
        if (!_mm256_testz_si256(diff, diff)) {
            // Mismatch found. Fall back to standard memcmp for this block to get the correct return value.
            return std::memcmp(p1 + i, p2 + i, simdWidth);
        }
    }
    // Handle remaining bytes
    return std::memcmp(p1 + i, p2 + i, numBytes - i);
#else
    // Fallback if no AVX2 support
    return std::memcmp(pNew, pPrev, numBytes);
#endif
}
