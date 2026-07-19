#pragma once
#include "avcodec_pipeline.h"
#include <chrono>

class AvcodecCpuPipeline : public AvcodecPipeline {
public:
    AvcodecCpuPipeline(Platform* platform);
    ~AvcodecCpuPipeline();

    bool Initialize(IScreenCapture* capture, int width, int height, int fps, int quality, float scale = 1.0f) override;

protected:
#ifdef USE_FFMPEG
    AVFrame* PrepareFrame(IScreenCapture* capture, int64_t& pts) override;
    void FinishFrame(AVFrame* frame) override;
#endif

private:
    bool InitInternal(int width, int height, int fps, int quality);
    void CleanupCpuResources();

#ifdef USE_FFMPEG
    AVFrame* m_cpuFrame = nullptr;
#endif
    
    // Prepare-stage detailed stats
    uint64_t m_captureTotalUs = 0;
    uint64_t m_copyTotalUs = 0;
    int m_statCount = 0;
    std::chrono::steady_clock::time_point m_lastLogTime = {};
};