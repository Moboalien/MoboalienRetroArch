#pragma once

#include "IEncodingPipeline.h"
#include "screen_capture.h"
#include "platform.h"

#ifdef USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavcodec/bsf.h>
}
#endif

class AvcodecPipeline : public IEncodingPipeline {
public:
    AvcodecPipeline(Platform* platform);
    virtual ~AvcodecPipeline();

    virtual bool Initialize(IScreenCapture* capture, int width, int height, int fps, int quality, float scale = 1.0f) = 0;
    bool ProcessFrame(IScreenCapture* capture, EncodedFrameInfo& outInfo, bool forceKeyframe) override;
    bool SubmitFrameForEncoding(IScreenCapture* capture, bool forceKeyframe) override;
    bool ReceiveEncodedFrames(EncodedFrameInfo& outInfo) override;
    const std::vector<uint8_t>& GetSpsPps() const override { return m_cachedSpsPps; }
    float GetScale() const override { return m_scale; }
    int GetQuality() const override { return m_quality; }
    void Cleanup() override;
    void EnableStreamSaving(bool enable) override;

protected:
#ifdef USE_FFMPEG
    virtual AVFrame* PrepareFrame(IScreenCapture* capture, int64_t& pts) = 0;
    virtual void FinishFrame(AVFrame* frame) = 0;
    bool InitializeCommon(const AVCodec* codec, int width, int height, int fps, int quality);
#endif

    Platform* m_platform = nullptr;
#ifdef USE_FFMPEG
    AVCodecContext* m_codecContext = nullptr;
    AVPacket* m_pkt = nullptr;
    AVBSFContext* m_bsf = nullptr;
    AVFrame* m_pendingFrame = nullptr;
#endif
    std::vector<uint8_t> m_cachedSpsPps;
    uint64_t m_startTimeMs = 0;
    FILE* m_outputFile = nullptr;
    bool m_saveStream = false;
    int m_width = 0;
    int m_height = 0;
    float m_scale = 1.0f;
    int m_quality = 23;
    int m_fps = 30;
    uint64_t m_prepareTotalUs = 0;
    uint64_t m_sendFrameTotalUs = 0;
    uint64_t m_receivePacketTotalUs = 0;
    uint64_t m_bsfTotalUs = 0;
private:
    uint64_t m_encodeTotalMs = 0;
    int m_encodeCount = 0;
    uint64_t m_encodeWindowStartMs = 0;
    int m_receivePacketCount = 0;
    uint64_t m_receiveWindowStartMs = 0;
};
