#pragma once
#include <cstdint>
#include "screen_capture.h"

enum StreamingMode {
    STREAMING_MJPEG = 0,
    STREAMING_DIFFERENTIAL = 1,
    STREAMING_H264 = 2,
};

struct AdaptiveQualityConfig {
    int quality;         // Base JPEG quality (1-100)
    int minQuality;      // Minimum quality threshold
    int qualityStep;     // Quality adjustment step
    int colorDepth;      // JPEG color depth (24 or 32 bits)
    AdaptiveQualityConfig() : quality(100), minQuality(30), qualityStep(1), colorDepth(24) {}
};

struct StreamerConfig {
    int fps;
    int bitrate;
    uint16_t port;
    StreamingMode streamingMode;
    AdaptiveQualityConfig adaptiveQuality;
    int tcpBufferSize;
    int maxClientBufferSize;
    bool captureScreenWithCursor;
    float scale;

    StreamerConfig() : fps(40), bitrate(2000000), port(0),
                       streamingMode(STREAMING_MJPEG), adaptiveQuality(), tcpBufferSize(1024 * 1024),
                       maxClientBufferSize(512 * 1024), captureScreenWithCursor(true), scale(1.0f) {}

};

// Map quality from 1-100 scale (100=best) to H.264 CRF/QP scale (0-51, lower=better)
// Quality 100 -> CRF 18 (visually lossless)
// Quality 75  -> CRF 23 (default, good quality)
// Quality 50  -> CRF 28 (medium quality)
// Quality 25  -> CRF 35 (low quality)
// Quality 1   -> CRF 51 (worst quality)
inline int MapQualityToH264(int quality) {
    if (quality <= 0) return 51;
    if (quality >= 100) return 18;
    // Linear mapping: quality 1-100 -> CRF 51-18
    return 51 - ((quality - 1) * 33) / 99;
}