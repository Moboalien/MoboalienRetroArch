#pragma once
#include "screenshare_protocol.h" // Updated include
#include "image_utils.h"

struct DiffResult {
    int x, y, width, height;
    bool hasChanges;
    Screenshare::FrameType type; // Updated namespace
};

class BitmapDiffer {
public:
    BitmapDiffer();
    ~BitmapDiffer();

    DiffResult Compare(const ImageUtils::RawImageFrame& newFrame);
    void Reset();

private:
    unsigned char* m_prevData;
    int m_dibWidth, m_dibHeight;
    int m_frameCount;
};
