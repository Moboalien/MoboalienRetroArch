#include "avcodec_cpu_pipeline.h"
#include "video_config.h"
#include "utils.h"
#include <chrono>

static const char* TAG = "AvcodecCpuPipeline";

#ifndef FF_PROFILE_H264_BASELINE
#define FF_PROFILE_H264_BASELINE 66
#endif

AvcodecCpuPipeline::AvcodecCpuPipeline(Platform* platform) : AvcodecPipeline(platform) {}

AvcodecCpuPipeline::~AvcodecCpuPipeline() {
    CleanupCpuResources();
}

void AvcodecCpuPipeline::CleanupCpuResources() {
#ifdef USE_FFMPEG
    if (m_cpuFrame) {
        av_frame_free(&m_cpuFrame);
        m_cpuFrame = nullptr;
    }
#endif
}

bool AvcodecCpuPipeline::Initialize(IScreenCapture* capture, int width, int height, int fps, int quality, float scale) {
#ifdef USE_FFMPEG
    m_scale = scale;

    // Discover actual dimensions from capture device first
    int targetWidth = 0, targetHeight = 0;
    ImageUtils::RawImageFrame frame;
    
    // Wait up to 3 seconds for the capture thread to produce at least one frame
    for (int i = 0; i < 30; ++i) {
        frame = capture->CaptureFrame(false, ImageUtils::PixelFormat::NV12);
        if (frame.data && frame.width > 0) break;
        m_platform->Sleep(100);
    }

    if (frame.data && frame.width > 0 && frame.height > 0) {
        targetWidth = frame.width;
        targetHeight = frame.height;
        LOGI(TAG, "Initialize: Using captured dimensions " + std::to_string(targetWidth) + "x" + std::to_string(targetHeight));
    } else {
        // Fallback to calculation if capture hasn't produced a frame yet
        targetWidth = static_cast<int>(width * scale) & ~1;
        targetHeight = static_cast<int>(height * scale) & ~1;
        LOGW(TAG, "Initialize: Capture empty, using calculated dimensions " + std::to_string(targetWidth) + "x" + std::to_string(targetHeight));
    }

    return InitInternal(targetWidth, targetHeight, fps, quality);
#else
    return false;
#endif
}

bool AvcodecCpuPipeline::InitInternal(int width, int height, int fps, int quality) {
#ifdef USE_FFMPEG
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (codec) LOGW(TAG, "Using default H.264 encoder");
    } else {
        LOGI(TAG, "Using libx264 encoder");
    }

    if (!codec || !InitializeCommon(codec, width, height, fps, quality)) {
        LOGE(TAG, "InitInternal: FFmpeg common initialization failed");
        return false;
    }

    m_codecContext->pix_fmt = AV_PIX_FMT_NV12;
    m_codecContext->profile = FF_PROFILE_H264_BASELINE;
    
    // Critical Latency Fix: Slice threading is required for low-latency streaming.
    // Frame threading adds latency equal to the number of threads.
    m_codecContext->thread_type = FF_THREAD_SLICE;

    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(m_codecContext->priv_data, "preset", "ultrafast", 0);
        av_opt_set(m_codecContext->priv_data, "tune", "zerolatency", 0);
        int h264Quality = MapQualityToH264(quality);
        av_opt_set_int(m_codecContext->priv_data, "crf", h264Quality, 0);
        
        // Force x264 to use multiple slices per frame. 
        av_opt_set(m_codecContext->priv_data, "slices", "4", 0);

        // Low Latency & CPU Optimization:
        // 1. intra-refresh: Smooths out spikes by refreshing the screen gradually instead of massive I-frames.
        // 2. no-deblock: Saves significant CPU by skipping the deblocking filter (good for high-motion gaming).
        // 3. forced-idr: Ensures consistent keyframe behavior.
        av_opt_set(m_codecContext->priv_data, "intra-refresh", "1", 0);
        av_opt_set(m_codecContext->priv_data, "no-deblock", "1", 0);
        av_opt_set(m_codecContext->priv_data, "forced-idr", "1", 0);
    }

    if (avcodec_open2(m_codecContext, codec, nullptr) < 0) {
        LOGE(TAG, "InitInternal: avcodec_open2 failed");
        return false;
    }

    const AVBitStreamFilter* filter = av_bsf_get_by_name("h264_mp4toannexb");
    if (!filter) {
        LOGE(TAG, "InitInternal: bitstream filter 'h264_mp4toannexb' not found");
        return false;
    }
    if (av_bsf_alloc(filter, &m_bsf) < 0) {
        LOGE(TAG, "InitInternal: av_bsf_alloc failed");
        return false;
    }
    if (avcodec_parameters_from_context(m_bsf->par_in, m_codecContext) < 0) {
        LOGE(TAG, "InitInternal: avcodec_parameters_from_context failed");
        return false;
    }
    m_bsf->time_base_in = m_codecContext->time_base;
    if (av_bsf_init(m_bsf) < 0) {
        LOGE(TAG, "InitInternal: av_bsf_init failed");
        return false;
    }

    m_cpuFrame = av_frame_alloc();
    if (!m_cpuFrame) {
        LOGE(TAG, "InitInternal: av_frame_alloc failed");
        return false;
    }
    m_cpuFrame->format = AV_PIX_FMT_NV12;
    m_cpuFrame->width = width;
    m_cpuFrame->height = height;
    if (av_frame_get_buffer(m_cpuFrame, 32) < 0) {
        LOGE(TAG, "InitInternal: av_frame_get_buffer failed");
        return false;
    }

    m_pkt = av_packet_alloc();
    if (!m_pkt) {
        LOGE(TAG, "InitInternal: av_packet_alloc failed");
        return false;
    }

    return true;
#else
    return false;
#endif
}

AVFrame* AvcodecCpuPipeline::PrepareFrame(IScreenCapture* capture, int64_t& pts) {
#ifdef USE_FFMPEG
    auto t0 = std::chrono::steady_clock::now();
    ImageUtils::RawImageFrame nv12 = capture->CaptureFrame(false, ImageUtils::PixelFormat::NV12);
    auto t1 = std::chrono::steady_clock::now();

    if (!nv12.data) return nullptr;

    // Use the actual capture timestamp for precise PTS
    if (nv12.timestampMs > 0) {
        if (m_startTimeMs == 0) m_startTimeMs = nv12.timestampMs;
        pts = static_cast<int64_t>(nv12.timestampMs - m_startTimeMs);
    }

    if (av_frame_make_writable(m_cpuFrame) < 0) {
        LOGE(TAG, "PrepareFrame: av_frame_make_writable failed");
        return nullptr;
    }

    // Optimization: Use av_image_copy which handles alignment and is optimized for SIMD
    const uint8_t* src_data[4] = { nv12.data, nv12.data + (size_t)nv12.stride * m_height, nullptr, nullptr };
    int src_linesize[4] = { nv12.stride, nv12.stride, 0, 0 };
    av_image_copy(m_cpuFrame->data, m_cpuFrame->linesize, src_data, src_linesize, AV_PIX_FMT_NV12, m_width, m_height);
    
    m_cpuFrame->pts = pts;
    
    auto t2 = std::chrono::steady_clock::now();
    m_captureTotalUs += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    m_copyTotalUs += std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    m_statCount++;

    if (std::chrono::duration_cast<std::chrono::seconds>(t2 - m_lastLogTime).count() >= 10) {
        if (m_statCount > 0) {
            LOGI(TAG, "Prepare detail avg: capture=" + std::to_string(m_captureTotalUs / m_statCount / 1000.0) + "ms, " +
                      "copy=" + std::to_string(m_copyTotalUs / m_statCount / 1000.0) + "ms");
        }
        m_captureTotalUs = m_copyTotalUs = 0;
        m_statCount = 0;
        m_lastLogTime = t2;
    }

    return m_cpuFrame;
#else
    return nullptr;
#endif
}

void AvcodecCpuPipeline::FinishFrame(AVFrame* frame) {}