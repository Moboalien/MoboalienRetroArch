#include "IEncodingPipeline.h"
#include "platform.h"
#ifdef USE_FFMPEG
#include "windows/avcodec_cpu_pipeline.h"
#endif
// #include "windows/avcodec_gpu_pipeline.h"

IEncodingPipeline* PipelineFactory::Create(PipelineType type, Platform* platform) {
    switch (type) {
#ifdef USE_FFMPEG
        case PipelineType::CPU:
            return new AvcodecCpuPipeline(platform);
        // case PipelineType::GPU:
        //     return new AvcodecGpuPipeline(platform);
#endif
        default:
            return nullptr;
    }
}
