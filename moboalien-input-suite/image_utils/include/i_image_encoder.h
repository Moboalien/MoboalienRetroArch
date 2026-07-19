#pragma once
#include <memory> // For std::unique_ptr

// Forward declarations to reduce header dependencies
namespace ImageUtils {
    struct RawImageFrame;
}
namespace Utils {
    struct Rect;
}

class IImageEncoder {
public:
    virtual ~IImageEncoder() = default;
    virtual bool EncodeToJPEG(const ImageUtils::RawImageFrame& frame, unsigned char** out_jpeg_data, size_t* out_jpeg_size, int quality, const Utils::Rect* crop_rect = nullptr) = 0;
    virtual bool EncodeToPNG(const ImageUtils::RawImageFrame& frame, unsigned char** out_png_data, size_t* out_png_size) = 0;
};

std::unique_ptr<IImageEncoder> CreateImageEncoder();