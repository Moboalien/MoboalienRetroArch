#include "avcodec_pipeline.h"
#include "utils.h"
#include "video_config.h"
#include <string>
#include <chrono>

static const char* TAG = "AvcodecPipeline";

AvcodecPipeline::AvcodecPipeline(Platform* platform) : m_platform(platform) {
#ifdef USE_FFMPEG
    m_outputFile = nullptr;
#endif
}

AvcodecPipeline::~AvcodecPipeline() {
    Cleanup();
}

void AvcodecPipeline::Cleanup() {
#ifdef USE_FFMPEG
    if (m_bsf) {
        av_bsf_free(&m_bsf);
        m_bsf = nullptr;
    }
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }
    if (m_pkt) {
        av_packet_free(&m_pkt);
        m_pkt = nullptr;
    }
    if (m_outputFile) {
        LOGI(TAG, "Closing debug file output.h264");
        fclose(m_outputFile);
        m_outputFile = nullptr;
    }
    m_cachedSpsPps.clear();
#endif
}

bool AvcodecPipeline::InitializeCommon(const AVCodec* codec, int width, int height, int fps, int quality) {
#ifdef USE_FFMPEG
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_quality = quality;

    if (m_saveStream && !m_outputFile) {
        m_outputFile = fopen("output.h264", "wb");
        if (m_outputFile) {
            LOGD(TAG, "Debugging: Saving H.264 stream to output.h264");
        }
    }

    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext) return false;

    // Map quality from 1-100 to H.264 scale (lower is better for H.264)
    int h264Quality = MapQualityToH264(quality);
    m_codecContext->global_quality = h264Quality;
    m_codecContext->width = width;
    m_codecContext->height = height;
    m_codecContext->time_base = {1, 1000};
    m_codecContext->framerate = {fps, 1};
    // Let the encoder use P-frames. zerolatency tune prevents buffering delay.
    m_codecContext->gop_size = 1; 
    m_codecContext->max_b_frames = 0;
    m_codecContext->thread_count = 0; // 0 = Auto (FFmpeg will use optimal number of threads based on logical cores)
    m_codecContext->delay = 0;
    m_codecContext->flags |= (AV_CODEC_FLAG_LOW_DELAY | AV_CODEC_FLAG_GLOBAL_HEADER);
    
    return true;
#else
    return false;
#endif
}

void AvcodecPipeline::EnableStreamSaving(bool enable) {
#ifdef USE_FFMPEG
    m_saveStream = enable;
#endif
}

bool AvcodecPipeline::ProcessFrame(IScreenCapture* capture, EncodedFrameInfo& outInfo, bool forceKeyframe) {
#ifdef USE_FFMPEG
    outInfo.data.clear();
    outInfo.isKeyFrame = false;
    outInfo.hasConfig = false;

    auto encodeStart = m_platform ? m_platform->GetTickCountMs() : 0;
    bool result = SubmitFrameForEncoding(capture, forceKeyframe) && ReceiveEncodedFrames(outInfo);

    if (m_platform) {
        uint64_t encodeEnd = m_platform->GetTickCountMs();
        m_encodeTotalMs += encodeEnd - encodeStart;
        ++m_encodeCount;
        if (m_encodeWindowStartMs == 0) m_encodeWindowStartMs = encodeStart;
        if (encodeEnd - m_encodeWindowStartMs >= 10000) {
            double avgMs     = m_encodeCount > 0 ? (double)m_encodeTotalMs        / m_encodeCount : 0.0;
            double avgPrep   = m_encodeCount > 0 ? (double)m_prepareTotalUs       / m_encodeCount / 1000.0 : 0.0;
            double avgSend   = m_encodeCount > 0 ? (double)m_sendFrameTotalUs     / m_encodeCount / 1000.0 : 0.0;
            double avgRecv   = m_encodeCount > 0 ? (double)m_receivePacketTotalUs / m_encodeCount / 1000.0 : 0.0;
            double avgBsf    = m_encodeCount > 0 ? (double)m_bsfTotalUs           / m_encodeCount / 1000.0 : 0.0;
            
            LOGI(TAG, "Encode avg over last 10s ("
                + std::to_string(m_encodeCount) + " frames): "
                + "total=" + std::to_string(avgMs) + "ms "
                + "prep=" + std::to_string(avgPrep) + "ms "
                + "send_frame=" + std::to_string(avgSend) + "ms "
                + "receive_packet=" + std::to_string(avgRecv) + "ms "
                + "bsf=" + std::to_string(avgBsf) + "ms");

            m_encodeTotalMs = 0;
            m_encodeCount = 0;
            m_prepareTotalUs = 0;
            m_sendFrameTotalUs = 0;
            m_receivePacketTotalUs = 0;
            m_bsfTotalUs = 0;
            m_encodeWindowStartMs = encodeEnd;
        }
    }

    return result;
#else
    return false;
#endif
}

bool AvcodecPipeline::SubmitFrameForEncoding(IScreenCapture* capture, bool forceKeyframe) {
#ifdef USE_FFMPEG
    if (!m_codecContext) {
        LOGE(TAG, "SubmitFrameForEncoding: Codec context is null");
        return false;
    }

    uint64_t now = m_platform ? m_platform->GetTickCountMs() : 0;
    if (m_startTimeMs == 0) m_startTimeMs = now;
    int64_t pts = static_cast<int64_t>(now - m_startTimeMs);
    
    auto t0 = std::chrono::steady_clock::now();
    AVFrame* frame = PrepareFrame(capture, pts);
    m_prepareTotalUs += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    
    if (!frame) return false;
    if (forceKeyframe) {
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->flags |= AV_FRAME_FLAG_KEY;
    } else {
        frame->pict_type = AV_PICTURE_TYPE_NONE;
        frame->flags &= ~AV_FRAME_FLAG_KEY;
    }

    auto t1 = std::chrono::steady_clock::now();
    int ret = avcodec_send_frame(m_codecContext, frame);
    m_sendFrameTotalUs += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t1).count();

    FinishFrame(frame);
    if (ret < 0) {
        LOGE(TAG, "SubmitFrameForEncoding: avcodec_send_frame failed: " + std::to_string(ret));
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool AvcodecPipeline::ReceiveEncodedFrames(EncodedFrameInfo& outInfo) {
#ifdef USE_FFMPEG
    int ret = 0;
    while (ret >= 0) {
        auto t1 = std::chrono::steady_clock::now();
        ret = avcodec_receive_packet(m_codecContext, m_pkt);
        m_receivePacketTotalUs += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t1).count();

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        else if (ret < 0) {
            LOGE(TAG, "ReceiveEncodedFrames: avcodec_receive_packet failed: " + std::to_string(ret));
            return false;
        }

        uint64_t now_ms = m_platform->GetTickCountMs();
        ++m_receivePacketCount;
        if (m_receiveWindowStartMs == 0) m_receiveWindowStartMs = now_ms;
        if (m_platform && now_ms - m_receiveWindowStartMs >= 10000) {
            double fps = m_receivePacketCount * 1000.0 / (now_ms - m_receiveWindowStartMs);
            LOGI(TAG, "ReceiveEncodedFrames: fps over last 10s = " + std::to_string(fps)
                + " (" + std::to_string(m_receivePacketCount) + " packets)");
            m_receivePacketCount = 0;
            m_receiveWindowStartMs = now_ms;
        }

        int bsf_ret = av_bsf_send_packet(m_bsf, m_pkt);
        if (bsf_ret < 0) {
            LOGE(TAG, "ReceiveEncodedFrames: av_bsf_send_packet failed: " + std::to_string(bsf_ret));
            av_packet_unref(m_pkt);
            return false;
        }

        while (bsf_ret >= 0) {
            auto t2 = std::chrono::steady_clock::now();
            bsf_ret = av_bsf_receive_packet(m_bsf, m_pkt);
            m_bsfTotalUs += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t2).count();

            if (bsf_ret == AVERROR(EAGAIN) || bsf_ret == AVERROR_EOF) break;
            else if (bsf_ret < 0) {
                LOGE(TAG, "ReceiveEncodedFrames: av_bsf_receive_packet failed: " + std::to_string(bsf_ret));
                return false;
            }

            if (m_pkt->flags & AV_PKT_FLAG_KEY) {
                outInfo.isKeyFrame = true;
                if (m_codecContext->extradata_size > 0) {
                    outInfo.data.insert(outInfo.data.end(), m_codecContext->extradata, m_codecContext->extradata + m_codecContext->extradata_size);
                    outInfo.hasConfig = true;
                    if (m_outputFile)
                        fwrite(m_codecContext->extradata, 1, m_codecContext->extradata_size, m_outputFile);
                }
            }
            outInfo.pts = m_pkt->pts;
            outInfo.data.insert(outInfo.data.end(), m_pkt->data, m_pkt->data + m_pkt->size);

            if (m_outputFile) {
                fwrite(m_pkt->data, 1, m_pkt->size, m_outputFile);
                fflush(m_outputFile);
            }

            if (outInfo.isKeyFrame && m_cachedSpsPps.empty() && m_codecContext->extradata_size > 0)
                m_cachedSpsPps.assign(m_codecContext->extradata, m_codecContext->extradata + m_codecContext->extradata_size);

            av_packet_unref(m_pkt);
        }
    }

    if (outInfo.data.empty()) {
        LOGD(TAG, "ReceiveEncodedFrames: No encoded data produced");
        return false;
    }
    return true;
#else
    return false;
#endif
}