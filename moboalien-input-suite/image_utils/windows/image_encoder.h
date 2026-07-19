#pragma once

#include <windows.h> // For ULONG_PTR
#include "i_image_encoder.h"
#include "rect.h" // For Rect definition

class ImageEncoder final : public IImageEncoder {
public:
    ImageEncoder();
    ~ImageEncoder();

    bool EncodeToJPEG(const ImageUtils::RawImageFrame& frame, unsigned char** out_jpeg_data, size_t* out_jpeg_size, int quality, const Utils::Rect* crop_rect) override;
    bool EncodeToPNG(const ImageUtils::RawImageFrame& frame, unsigned char** out_png_data, size_t* out_png_size) override;

private:
    ULONG_PTR m_gdiplusToken;
    CLSID m_pngClsid;
    CLSID m_jpegClsid;
    int ImageEncoder::GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
};