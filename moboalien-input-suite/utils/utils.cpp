#include "utils.h"
#include <cstdio> // For FILE, fopen, fwrite, fclose
#include <cstring> // For memcpy
#include <iostream> // For std::cout
#include <random>

void DumpDataToFile(const char* filename, const void* data, size_t size) {
    FILE* file = fopen(filename, "wb");
    if (file) {
        // Use the more robust fwrite signature (item_size=1, num_items=size)
        fwrite(data, 1, size, file);
        fclose(file);
    }
}

// Helper to write a 32-bit integer in network byte order (big-endian)
void AppendInt32(std::vector<char>& vec, int32_t value) { // NOLINT: Allow non-const ref for output
    vec.push_back((value >> 24) & 0xFF);
    vec.push_back((value >> 16) & 0xFF);
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

void AppendInt32(std::vector<uint8_t>& vec, int32_t value) { // NOLINT: Allow non-const ref for output
    vec.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    vec.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    vec.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    vec.push_back(static_cast<uint8_t>(value & 0xFF));
} 

// Helper to write a 16-bit integer in network byte order (big-endian)
void AppendInt16(std::vector<char>& vec, int16_t value) { // NOLINT: Allow non-const ref for output
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

void AppendInt16(std::vector<uint8_t>& vec, int16_t value) { // NOLINT: Allow non-const ref for output
    vec.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    vec.push_back(static_cast<uint8_t>(value & 0xFF));
} 

// Helper to read a 16-bit integer from a buffer in network byte order (big-endian)
int16_t ReadInt16(const char* buffer) {
    const auto* p = reinterpret_cast<const unsigned char*>(buffer);
    return (static_cast<int16_t>(p[0]) << 8) |
           (static_cast<int16_t>(p[1]));
}

// Helper to read a 32-bit integer from a buffer in network byte order (big-endian)
int32_t ReadInt32(const char* buffer) {
    // Cast to unsigned char to prevent sign extension on systems where char is signed
    const auto* p = reinterpret_cast<const unsigned char*>(buffer);
    return (static_cast<int32_t>(p[0]) << 24) |
           (static_cast<int32_t>(p[1]) << 16) |
           (static_cast<int32_t>(p[2]) << 8)  |
           (static_cast<int32_t>(p[3]));
}

void AppendInt64(std::vector<char>& vec, int64_t value) {
    vec.push_back((value >> 56) & 0xFF);
    vec.push_back((value >> 48) & 0xFF);
    vec.push_back((value >> 40) & 0xFF);
    vec.push_back((value >> 32) & 0xFF);
    vec.push_back((value >> 24) & 0xFF);
    vec.push_back((value >> 16) & 0xFF);
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

void AppendInt64(std::vector<uint8_t>& vec, int64_t value) {
    vec.push_back(static_cast<uint8_t>((value >> 56) & 0xFF));
    vec.push_back(static_cast<uint8_t>((value >> 48) & 0xFF));
    vec.push_back(static_cast<uint8_t>((value >> 40) & 0xFF));
    vec.push_back(static_cast<uint8_t>((value >> 32) & 0xFF));
    vec.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    vec.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    vec.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    vec.push_back(static_cast<uint8_t>(value & 0xFF));
}

int64_t ReadInt64(const char* buffer) {
    const auto* p = reinterpret_cast<const unsigned char*>(buffer);
    return (static_cast<int64_t>(p[0]) << 56) |
           (static_cast<int64_t>(p[1]) << 48) |
           (static_cast<int64_t>(p[2]) << 40) |
           (static_cast<int64_t>(p[3]) << 32) |
           (static_cast<int64_t>(p[4]) << 24) |
           (static_cast<int64_t>(p[5]) << 16) |
           (static_cast<int64_t>(p[6]) << 8)  |
           (static_cast<int64_t>(p[7]));
}

void AppendFloat(std::vector<char>& vec, float value) {
    uint32_t as_int;
    // Use memcpy to safely copy the bit pattern of the float to an integer.
    // This avoids breaking strict-aliasing rules.
    memcpy(&as_int, &value, sizeof(as_int));
    // Convert the integer to network byte order.
    // Manually append the integer in network byte order (big-endian).
    vec.push_back((as_int >> 24) & 0xFF);
    vec.push_back((as_int >> 16) & 0xFF);
    vec.push_back((as_int >> 8) & 0xFF);
    vec.push_back(as_int & 0xFF);
}

void AppendFloat(std::vector<uint8_t>& vec, float value) {
    uint32_t as_int;
    memcpy(&as_int, &value, sizeof(as_int));
    vec.push_back(static_cast<uint8_t>((as_int >> 24) & 0xFF));
    vec.push_back(static_cast<uint8_t>((as_int >> 16) & 0xFF));
    vec.push_back(static_cast<uint8_t>((as_int >> 8) & 0xFF));
    vec.push_back(static_cast<uint8_t>(as_int & 0xFF));
}

float ReadFloat(const char* buffer) {
    // Manually read the integer from the buffer in network byte order (big-endian).
    const auto* p = reinterpret_cast<const unsigned char*>(buffer);
    uint32_t host_int = (static_cast<uint32_t>(p[0]) << 24) |
                        (static_cast<uint32_t>(p[1]) << 16) |
                        (static_cast<uint32_t>(p[2]) << 8)  |
                        (static_cast<uint32_t>(p[3]));
    float value;
    memcpy(&value, &host_int, sizeof(value));
    return value;
}

std::string BytesToHex(const uint8_t* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    std::string s(len * 2, ' ');
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];
        s[i * 2] = hex[b >> 4];
        s[i * 2 + 1] = hex[b & 0xF];
    }
    return s;
}

std::string BytesToHex(const std::vector<uint8_t>& data) {
    return BytesToHex(data.data(), data.size());
}

std::optional<std::vector<uint8_t>> HexToBytes(const std::string& hex) {
    if (hex.size() % 2 != 0) return std::nullopt;
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<uint8_t> out(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hexVal(hex[i]);
        int lo = hexVal(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out[i / 2] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return out;
}

uint32_t HashData(const unsigned char *data, uint32_t size, uint32_t sampleStep) {
    if (!data || size == 0) {
        return 0;
    }
    // Simple FNV-1a hash on sampled data for performance.
    // Cursors are small and distinct enough that sampling is sufficient
    // to detect changes without iterating over every byte.
    uint32_t hash = 2166136261u;

    for (size_t i = 0; i < size; i += sampleStep) {
        hash ^= data[i];
        hash *= 16777619;
    }
    return hash;
}

std::string GenerateRandomSalt(size_t length) {
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    
    std::string salt(length, ' ');
    for (size_t i = 0; i < length; ++i) {
        salt[i] = charset[dis(gen)];
    }
    return salt;
}