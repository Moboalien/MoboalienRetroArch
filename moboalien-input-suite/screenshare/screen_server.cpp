#include "http_streamer.h"
#include "differential_streamer.h"
#ifdef USE_FFMPEG
#include "avcodec_streamer.h"
#endif
#include "screen_capture.h"
#include "video_config.h"
#include "platform.h"
#include "utils.h"
#include <string>
#include <iostream>

static const char* TAG = "ScreenServer";

void printUsage() {
    std::cout << "Screen Share Server - Moboalien Input Suite\n"
              << "Usage: screen_server [options]\n"
              << "Options:\n"
              << "  --fps <fps>         Frame rate (default: 30)\n"
              << "  --bitrate <kbps>    Bitrate in kbps (default: 2000)\n"
              << "  --port <port>       Server port (default: 8080)\n"
              << "  --scale <factor>    Resolution scale (default: 1.0, 0.5=half)\n"
              << "  --quality <1-100>   JPEG quality (default: 75)\n"
              << "  --method <gdi|dd>   Capture method: gdi or dd (default: gdi)\n"
              << "  --streaming <mjpeg|diff|h264> Streaming mode: mjpeg, diff or h264 (default: mjpeg)\n"
              << "  --cursor <true|false> Capture screen with cursor (default: true)\n"
              << "  --maxsize <bytes>   Max frame size for adaptive quality (default: 200000)\n"
              << "  --minsize <bytes>   Min frame size for adaptive quality (default: 100000)\n"
              << "  --minquality <1-100> Min quality threshold (default: 30)\n"
              << "  --clientbuffer <kb> Max per-client send buffer in KB (default: 5120)\n"
              << "  --cpu               Enforce software encoding (CPU pipeline)\n"
              << "  --savestream        Save H.264 stream to output.h264 file\n"
              << "  --help              Show this help\n\n"
              << "Connect with VLC: http://localhost:8080/stream\n";
}

int main(int argc, char* argv[]) {
    // Initialize logger
    Logger& logger = Logger::GetInstance();
    logger.SetLogLevel(LogLevel::INFO);
    logger.SetOutputToConsole(true);
    
    CaptureConfig captureConfig;
    StreamerConfig streamerConfig;
    bool forceCpu = false;
    bool saveStream = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            printUsage();
            return 0;
        }
        else if (arg == "--fps" && i + 1 < argc) {
            streamerConfig.fps = atoi(argv[++i]);
        }
        else if (arg == "--bitrate" && i + 1 < argc) {
            streamerConfig.bitrate = atoi(argv[++i]) * 1000; // Convert kbps to bps
        }
        else if (arg == "--port" && i + 1 < argc) {
            int port = atoi(argv[++i]);
            streamerConfig.port = static_cast<uint16_t>(port);
        }
        else if (arg == "--scale" && i + 1 < argc) {
            captureConfig.scale = (float)atof(argv[++i]);
        }
        else if (arg == "--quality" && i + 1 < argc) {
            streamerConfig.adaptiveQuality.quality = atoi(argv[++i]);
        }
        else if (arg == "--method" && i + 1 < argc) {
            std::string method = argv[++i];
            if (method == "dd" || method == "desktop" || method == "duplication") {
                captureConfig.method = CAPTURE_DESKTOP_DUPLICATION;
            } else {
                captureConfig.method = CAPTURE_GDI;
            }
        }
        else if (arg == "--cursor" && i + 1 < argc) {
            std::string method = argv[++i];
            if (method == "true" || method == "1") {
                streamerConfig.captureScreenWithCursor = true;
            } else {
                streamerConfig.captureScreenWithCursor = false;
            }
        }
        else if (arg == "--minquality" && i + 1 < argc) {
            streamerConfig.adaptiveQuality.minQuality = atoi(argv[++i]);
        }
        else if (arg == "--streaming" && i + 1 < argc) {
            std::string mode = argv[++i];
            if (mode == "diff" || mode == "differential") {
                streamerConfig.streamingMode = STREAMING_DIFFERENTIAL;
            } else if (mode == "h264" || mode == "avcodec") {
                streamerConfig.streamingMode = STREAMING_H264;
            } else {
                streamerConfig.streamingMode = STREAMING_MJPEG;
            }
        }
        else if (arg == "--clientbuffer" && i + 1 < argc) {
            streamerConfig.maxClientBufferSize = atoi(argv[++i]) * 1024; // Convert KB to bytes
        }
        else if (arg == "--cpu") {
            forceCpu = true;
        }
        else if (arg == "--savestream") {
            saveStream = true;
        }
    }
    
    LOGI(TAG, "Starting Screen Share Server...");
    LOGI(TAG, "Capture Scale: " + std::to_string(captureConfig.scale));
    LOGI(TAG, "FPS: " + std::to_string(streamerConfig.fps));
    LOGI(TAG, "Bitrate: " + std::to_string(streamerConfig.bitrate / 1000) + " kbps");
    LOGI(TAG, "Port: " + std::to_string(static_cast<int>(streamerConfig.port)));
    LOGI(TAG, "Streaming Mode: " + std::string(streamerConfig.streamingMode == STREAMING_DIFFERENTIAL ? "Differential" : (streamerConfig.streamingMode == STREAMING_H264 ? "H264" : "MJPEG")));
    LOGI(TAG, "JPEG Quality: " + std::to_string(streamerConfig.adaptiveQuality.quality));
    LOGI(TAG, "Capture Method: " + std::string(captureConfig.method == CAPTURE_DESKTOP_DUPLICATION ? "Desktop Duplication" : "GDI"));
    
    auto platform = CreatePlatform();
    platform->SetProcessHighPriority();
    auto imageEncoder = CreateImageEncoder();
    auto capture = CreateScreenCapture(platform.get());
    BaseStreamer* streamer = nullptr;

    if (!capture->Initialize(captureConfig)) {
        LOGE(TAG, "Failed to initialize screen capture");
        return 1; // platform and imageEncoder will be cleaned up by unique_ptr
    }
    
    if (streamerConfig.streamingMode == STREAMING_MJPEG) {
        streamer = new HTTPStreamer(streamerConfig, capture.get(), platform.get(), imageEncoder.get());
    } else if (streamerConfig.streamingMode == STREAMING_H264) {
#ifdef USE_FFMPEG
        auto avStreamer = new AvcodecStreamer(streamerConfig, capture.get(), platform.get(), imageEncoder.get());
        if (forceCpu) {
            avStreamer->SetForceCpuEncoding(true);
        }
        if (saveStream) {
            avStreamer->EnableStreamSaving(true);
        }
        streamer = avStreamer;
#else
        LOGE(TAG, "H264 streaming not supported in this build. Falling back to Differential.");
        streamer = new DifferentialStreamer(streamerConfig, capture.get(), platform.get(), imageEncoder.get());
#endif
    } else {
        streamer = new DifferentialStreamer(streamerConfig, capture.get(), platform.get(), imageEncoder.get());
    }

    streamer->Start();

    LOGI(TAG, "Screen sharing active. Press Enter to stop...");
    std::cin.get();
    
    streamer->Stop();

    delete streamer;
    streamer = nullptr;

    LOGI(TAG, "Screen sharing stopped.");
    
    return 0;
}