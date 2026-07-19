#pragma once
#include <cstdint>
#include <vector>
#include <memory>

namespace ImageUtils {

enum class PixelFormat {
        BGRA32,
        RGBA32,
        RGB24,
        NV12
    };

struct RawImageFrame {
    int width;
    int height;
    int stride; // Bytes per row
    PixelFormat format;
    const unsigned char* data; // Points into sharedData if non-empty, otherwise non-owning
    std::shared_ptr<std::vector<unsigned char>> sharedData;
    uint64_t timestampMs;

    RawImageFrame() : width(0), height(0), stride(0), format(PixelFormat::BGRA32), data(nullptr), timestampMs(0) {}
};

} // namespace ImageUtils