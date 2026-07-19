#include "image_encoder.h"
#include "utils.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include "platform.h"
#include "image_utils.h"
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef USE_LIBJPEG_TURBO
#include <jpeglib.h>
#include <turbojpeg.h>
#endif
#include <chrono>
using namespace Gdiplus;

static const char* TAG = "ImageEncoder";

#ifdef USE_LIBJPEG_TURBO
// RAII wrapper for thread-local compressor to handle cleanup automatically
struct ScopedTJCompressor {
    tjhandle handle;
    ScopedTJCompressor() { handle = tjInitCompress(); }
    ~ScopedTJCompressor() { if (handle) tjDestroy(handle); }
};
#endif

bool ImageEncoder::EncodeToJPEG(
    const ImageUtils::RawImageFrame& frame,
    unsigned char** jpegData,
    size_t* jpegSize,
    int quality,
    const Utils::Rect* cropRect
) {
    // Use a thread-local buffer to avoid allocation every frame.
    // The caller should NOT delete *jpegData.
    static thread_local std::vector<unsigned char> t_jpegBuffer;

#ifdef USE_LIBJPEG_TURBO
    static bool logged = false;
    if (!logged) {
#ifdef LIBJPEG_TURBO_WITH_SIMD
        LOGI(TAG, "Using libjpeg-turbo (SIMD Enabled)");
#else
        LOGI(TAG, "Using libjpeg-turbo (SIMD Disabled)");
#endif
        logged = true;
    }
    
    // Use a thread-local compressor to avoid init/destroy overhead every frame.
    static thread_local ScopedTJCompressor t_compressor;
    if (!t_compressor.handle) {
        LOGE(TAG, "Failed to initialize TurboJPEG compressor");
        return false;
    }
    
    int x = 0;
    int y = 0;
    int w = frame.width;
    int h = frame.height;
    
    if (cropRect) {
        x = cropRect->left;
        y = cropRect->top;
        w = cropRect->right - cropRect->left;
        h = cropRect->bottom - cropRect->top;
    }
    
    int pixelFormat = TJPF_RGBA;
    if (frame.format == ImageUtils::PixelFormat::BGRA32) {
        pixelFormat = TJPF_BGRA;
    }
    
    // Calculate the maximum possible size for the JPEG buffer.
    // This allows us to allocate the destination buffer directly and avoid
    // TurboJPEG's internal allocation + our subsequent memcpy.
    unsigned long maxBufSize = tjBufSize(w, h, TJSAMP_420);
    
    if (t_jpegBuffer.size() < maxBufSize) {
        t_jpegBuffer.resize(maxBufSize);
    }
    unsigned char* dstBuf = t_jpegBuffer.data();
    
    unsigned long compressedSize = maxBufSize;
    
    // Point to the start of the data (handling crop offset)
    const unsigned char* srcPtr = frame.data + (y * frame.stride) + (x * 4);
    
    // Compress using Fast DCT for speed, but only for lower qualities.
    // For quality >= 96, Fast DCT disables SIMD quantization in libjpeg-turbo,
    // making it slower than Accurate DCT.
    // TJFLAG_NOREALLOC ensures it writes to our buffer and doesn't try to reallocate.
    int flags = TJFLAG_NOREALLOC;
    if (quality < 96) {
        flags |= TJFLAG_FASTDCT;
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    int ret = tjCompress2(t_compressor.handle, srcPtr, w, frame.stride, h, pixelFormat, 
                          &dstBuf, &compressedSize, TJSAMP_420, quality, flags);
    auto end = std::chrono::high_resolution_clock::now();

    static int logCounter = 0;
    if (++logCounter % 60 == 0) {
        double ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        LOGV(TAG, "JPEG Encode: " + std::to_string(ms) + "ms (Q=" + std::to_string(quality) + ")");
    }
    
    if (ret == 0) {
        *jpegSize = compressedSize;
        *jpegData = dstBuf;
        return true;
    }
    
    LOGE(TAG, "TurboJPEG compression failed: " + std::string(tjGetErrorStr2(t_compressor.handle)));
    return false;
#else
    static bool logged = false;
    if (!logged) {
        LOGI(TAG, "Using GDI+ for JPEG encoding");
        logged = true;
    }
    static bool gdiplusInitialized = false;
    static CLSID m_jpegClsid;
    static bool jpegClsidInitialized = false;

    if (!gdiplusInitialized) {
        GdiplusStartupInput gdiplusStartupInput;
        GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);
        gdiplusInitialized = true;
    }

    if (!jpegClsidInitialized) {
        if (GetEncoderClsid(L"image/jpeg", &m_jpegClsid) == -1) {
            LOGE(TAG, "Failed to get JPEG encoder CLSID");
        }
        jpegClsidInitialized = true;
    }

    IStream* jpegStream = NULL;
    HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &jpegStream);
    if (FAILED(hr) || !jpegStream) {
        LOGE(TAG, "Failed to create JPEG stream: 0x" + std::to_string(hr));
        return false;
    }

    const unsigned char* imageData = frame.data;
    std::vector<unsigned char> swizzledBuffer;

    // Determine GDI+ pixel format from our enum
    Gdiplus::PixelFormat gdiPixelFormat = PixelFormat32bppARGB; // Default for BGRA
    if (frame.format == ImageUtils::PixelFormat::RGBA32) {
        // GDI+ expects BGRA (PixelFormat32bppARGB). We have RGBA, so we need to swizzle R and B.
        size_t bufferSize = (size_t)frame.height * frame.stride;
        swizzledBuffer.resize(bufferSize);
        const uint32_t* src = reinterpret_cast<const uint32_t*>(frame.data);
        uint32_t* dst = reinterpret_cast<uint32_t*>(swizzledBuffer.data());
        size_t numPixels = bufferSize / 4;
        for (size_t i = 0; i < numPixels; ++i) {
            uint32_t pixel = src[i];
            // Swizzle R (bits 0-7) and B (bits 16-23)
            dst[i] = (pixel & 0xFF00FF00) | ((pixel & 0x000000FF) << 16) | ((pixel & 0x00FF0000) >> 16);
        }
        imageData = swizzledBuffer.data();
    }

    // Create a GDI+ bitmap from the raw frame data.
    // PixelFormat32bppARGB is correct for BGRA data on little-endian systems like Windows.
    Bitmap srcBitmap(
        frame.width,
        frame.height,
        frame.stride,
        gdiPixelFormat,
        const_cast<BYTE*>(imageData)
    );

    if (srcBitmap.GetLastStatus() != Ok) {
        LOGE(TAG, "Failed to create JPEG bitmap from raw data");
        jpegStream->Release();
        return false;
    }

    Bitmap* bitmapToSave = &srcBitmap;

    if (cropRect) {
        UINT cropWidth = cropRect->right - cropRect->left;
        UINT cropHeight = cropRect->bottom - cropRect->top;

        Bitmap* cropped = new Bitmap(cropWidth, cropHeight, srcBitmap.GetPixelFormat());
        Graphics g(cropped);
        // Clear the destination bitmap with a transparent color. This is crucial
        // to prevent the alpha-blended cursor from being drawn on a black background.
        g.Clear(Color(0, 0, 0, 0));

        // Set compositing mode to SourceCopy to ensure the alpha channel (used for the cursor)
        // is copied directly without blending, which would otherwise result in a black box
        // around the cursor.
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.DrawImage(&srcBitmap,
                    Gdiplus::Rect(0, 0, cropWidth, cropHeight),
                    cropRect->left, cropRect->top, cropWidth, cropHeight,
                    UnitPixel);
        bitmapToSave = cropped;
    }

    EncoderParameters encoderParams;
    encoderParams.Count = 1;
    encoderParams.Parameter[0].Guid = EncoderQuality;
    encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParams.Parameter[0].NumberOfValues = 1;
    ULONG qualityValue = quality;
    encoderParams.Parameter[0].Value = &qualityValue;

    Status status = bitmapToSave->Save(jpegStream, &m_jpegClsid, &encoderParams);

    if (status == Ok) {
        HGLOBAL hGlobal;
        hr = GetHGlobalFromStream(jpegStream, &hGlobal);
        if (FAILED(hr)) {
            LOGE(TAG, "Failed to get global from JPEG stream: 0x" + std::to_string(hr));
        } else {
            *jpegSize = GlobalSize(hGlobal);
            
            if (t_jpegBuffer.size() < *jpegSize) {
                t_jpegBuffer.resize(*jpegSize);
            }
            *jpegData = t_jpegBuffer.data();

            void* pData = GlobalLock(hGlobal);
            if (pData) {
                memcpy(*jpegData, pData, *jpegSize);
                GlobalUnlock(hGlobal);
            } else {
                LOGE(TAG, "Failed to lock JPEG global memory");
                *jpegData = NULL;
                *jpegSize = 0;
            }
        }
    } else {
        LOGE(TAG, "JPEG bitmap save failed with status: " + std::to_string(status));
    }

    if (bitmapToSave != &srcBitmap)
        delete bitmapToSave;
    jpegStream->Release();
    return status == Ok;
#endif
}

ImageEncoder::ImageEncoder() : m_gdiplusToken(0) {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);
    if (GetEncoderClsid(L"image/png", &m_pngClsid) == -1) {
        LOGE(TAG, "Failed to get PNG encoder CLSID");
    }
    if (GetEncoderClsid(L"image/jpeg", &m_jpegClsid) == -1) {
        LOGE(TAG, "Failed to get JPEG encoder CLSID");
    }
}

bool ImageEncoder::EncodeToPNG(
    const ImageUtils::RawImageFrame& frame,
    unsigned char** pngData,
    size_t* pngSize
) {
    static bool gdiplusInitialized = false;
    static ULONG_PTR gdiplusToken;
    static CLSID m_pngClsid;
    static bool pngClsidInitialized = false;

    if (!gdiplusInitialized) {
        GdiplusStartupInput gdiplusStartupInput;
        GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
        gdiplusInitialized = true;
    }

    if (!pngClsidInitialized) {
        if (GetEncoderClsid(L"image/png", &m_pngClsid) == -1) {
            LOGE(TAG, "Failed to get PNG encoder CLSID");
        }
        pngClsidInitialized = true;
    }

    IStream* pngStream = NULL;
    HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &pngStream);
    if (FAILED(hr) || !pngStream) {
        LOGE(TAG, "Failed to create PNG stream: 0x" + std::to_string(hr));
        return false;
    }

    const unsigned char* imageData = frame.data;
    std::vector<unsigned char> swizzledBuffer;

    // Determine GDI+ pixel format from our enum
    Gdiplus::PixelFormat gdiPixelFormat = PixelFormat32bppARGB; // Default for BGRA
    if (frame.format == ImageUtils::PixelFormat::RGBA32) {
        // GDI+ expects BGRA (PixelFormat32bppARGB). We have RGBA, so we need to swizzle R and B.
        size_t bufferSize = (size_t)frame.height * frame.stride;
        swizzledBuffer.resize(bufferSize);
        const uint32_t* src = reinterpret_cast<const uint32_t*>(frame.data);
        uint32_t* dst = reinterpret_cast<uint32_t*>(swizzledBuffer.data());
        size_t numPixels = bufferSize / 4;
        for (size_t i = 0; i < numPixels; ++i) {
            uint32_t pixel = src[i];
            // Swizzle R (bits 0-7) and B (bits 16-23)
            dst[i] = (pixel & 0xFF00FF00) | ((pixel & 0x000000FF) << 16) | ((pixel & 0x00FF0000) >> 16);
        }
        imageData = swizzledBuffer.data();
    }

    Bitmap srcBitmap(frame.width, frame.height, frame.stride, gdiPixelFormat, const_cast<BYTE*>(imageData));
    if (srcBitmap.GetLastStatus() != Ok) {
        LOGE(TAG, "Failed to create PNG bitmap from raw data");
        pngStream->Release();
        return false;
    }

    Status status = srcBitmap.Save(pngStream, &m_pngClsid, NULL);

    if (status == Ok) {
        HGLOBAL hGlobal;
        hr = GetHGlobalFromStream(pngStream, &hGlobal);
        if (SUCCEEDED(hr)) {
            *pngSize = GlobalSize(hGlobal);
            *pngData = new unsigned char[*pngSize];
            void* pData = GlobalLock(hGlobal);
            if (pData) {
                memcpy(*pngData, pData, *pngSize);
                GlobalUnlock(hGlobal);
            }
        }
    }
    pngStream->Release();
    return status == Ok;
}

int ImageEncoder::GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    
    GetImageEncodersSize(&num, &size);
    if (size == 0) {
        LOGE(TAG, "No image encoders found");
        return -1;
    }
    
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) {
        LOGE(TAG, "Failed to allocate memory for codec info");
        return -1;
    }
    
    GetImageEncoders(num, size, pImageCodecInfo);
    
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    
    free(pImageCodecInfo);
    return -1;
}

ImageEncoder::~ImageEncoder() { // This is now correct
    if (m_gdiplusToken != 0) { 
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
    }
}

std::unique_ptr<IImageEncoder> CreateImageEncoder() {
    return std::make_unique<ImageEncoder>();
}