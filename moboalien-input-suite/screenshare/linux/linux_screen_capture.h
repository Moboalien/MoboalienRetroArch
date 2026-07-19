#pragma once

#include "screen_capture.h"
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <vector>

class LnuxScreenCapture : public IScreenCapture {
public:
    LnuxScreenCapture();
    ~LnuxScreenCapture();

    bool Initialize(const CaptureConfig& config) override;
    RawImageFrame CaptureFrame() override;

private:
    void Cleanup();
    void DrawCursor();

    CaptureConfig m_config;
    Display* m_display;
    Window m_root;
    XImage* m_xImage;
    XShmSegmentInfo m_shmInfo;

    int m_width;
    int m_height;
    int m_scaledWidth;
    int m_scaledHeight;

    std::vector<unsigned char> m_scaledBuffer;
    RawImageFrame m_frame;
};