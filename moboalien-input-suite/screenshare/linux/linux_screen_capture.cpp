#include "linux_screen_capture.h"
#include <iostream>
#include <cstring>
#include <X11/Xutil.h>

LnuxScreenCapture::LnuxScreenCapture()
    : m_display(nullptr), m_root(0), m_xImage(nullptr),
      m_width(0), m_height(0), m_scaledWidth(0), m_scaledHeight(0) {
    m_shmInfo.shmaddr = (char*)-1;
}

LnuxScreenCapture::~LnuxScreenCapture() {
    Cleanup();
}

void LnuxScreenCapture::Cleanup() {
    if (m_display) {
        if (m_xImage) {
            XDestroyImage(m_xImage);
            m_xImage = nullptr;
        }
        if (m_shmInfo.shmaddr != (char*)-1) {
            XShmDetach(m_display, &m_shmInfo);
            shmdt(m_shmInfo.shmaddr);
            m_shmInfo.shmaddr = (char*)-1;
        }
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
}

bool LnuxScreenCapture::Initialize(const CaptureConfig& config) {
    m_config = config;

    // Required for multi-threaded X11 access
    if (!XInitThreads()) {
        std::cerr << "XInitThreads failed." << std::endl;
        return false;
    }

    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        std::cerr << "Cannot open X display." << std::endl;
        return false;
    }

    if (!XShmQueryExtension(m_display)) {
        std::cerr << "MIT-SHM extension is not available." << std::endl;
        Cleanup();
        return false;
    }

    m_root = DefaultRootWindow(m_display);

    XWindowAttributes gwa;
    XGetWindowAttributes(m_display, m_root, &gwa);
    m_width = gwa.width;
    m_height = gwa.height;

    m_scaledWidth = m_width * m_config.scale;
    m_scaledHeight = m_height * m_config.scale;

    m_xImage = XShmCreateImage(m_display, gwa.visual, gwa.depth, ZPixmap, nullptr, &m_shmInfo, m_width, m_height);
    if (!m_xImage) {
        std::cerr << "XShmCreateImage failed." << std::endl;
        Cleanup();
        return false;
    }

    m_shmInfo.shmid = shmget(IPC_PRIVATE, m_xImage->bytes_per_line * m_xImage->height, IPC_CREAT | 0777);
    if (m_shmInfo.shmid < 0) {
        std::cerr << "shmget failed." << std::endl;
        Cleanup();
        return false;
    }

    m_shmInfo.shmaddr = (char*)shmat(m_shmInfo.shmid, nullptr, 0);
    m_xImage->data = m_shmInfo.shmaddr;
    m_shmInfo.readOnly = False;

    if (!XShmAttach(m_display, &m_shmInfo)) {
        std::cerr << "XShmAttach failed." << std::endl;
        Cleanup();
        return false;
    }

    // Mark the shared memory segment for deletion after the process detaches
    shmctl(m_shmInfo.shmid, IPC_RMID, 0);

    if (m_config.scale < 1.0f) {
        m_scaledBuffer.resize(m_scaledWidth * m_scaledHeight * 4);
    }

    std::cout << "Initialized X11 capture with SHM: " << m_width << "x" << m_height << std::endl;
    return true;
}

void LnuxScreenCapture::DrawCursor() {
    if (!m_config.captureScreenWithCursor) return;

    XFixesCursorImage* cursor = XFixesGetCursorImage(m_display);
    if (!cursor) return;

    int cursor_x = cursor->x - cursor->xhot;
    int cursor_y = cursor->y - cursor->yhot;

    for (int y = 0; y < cursor->height; ++y) {
        for (int x = 0; x < cursor->width; ++x) {
            int screen_x = cursor_x + x;
            int screen_y = cursor_y + y;

            if (screen_x >= 0 && screen_x < m_width && screen_y >= 0 && screen_y < m_height) {
                unsigned long pixel = cursor->pixels[y * cursor->width + x];
                unsigned char alpha = (pixel >> 24) & 0xFF;

                if (alpha > 0) { // Simple alpha blending
                    unsigned char* screen_pixel = (unsigned char*)m_xImage->data + (screen_y * m_xImage->bytes_per_line) + (screen_x * 4);
                    screen_pixel[0] = (unsigned char)(((pixel & 0xFF) * alpha + screen_pixel[0] * (255 - alpha)) / 255); // B
                    screen_pixel[1] = (unsigned char)((((pixel >> 8) & 0xFF) * alpha + screen_pixel[1] * (255 - alpha)) / 255); // G
                    screen_pixel[2] = (unsigned char)((((pixel >> 16) & 0xFF) * alpha + screen_pixel[2] * (255 - alpha)) / 255); // R
                }
            }
        }
    }

    XFree(cursor);
}

RawImageFrame LnuxScreenCapture::CaptureFrame(bool withCursor) {
    if (!m_display || !XShmGetImage(m_display, m_root, m_xImage, 0, 0, AllPlanes)) {
        return RawImageFrame();
    }

    DrawCursor();

    m_frame.width = m_width;
    m_frame.height = m_height;
    m_frame.stride = m_xImage->bytes_per_line;
    m_frame.format = PixelFormat::BGRA32; // XImage with 32-bit depth is typically BGRA
    m_frame.data = (const unsigned char*)m_xImage->data;

    // Handle scaling if required (simple nearest neighbor)
    if (m_config.scale < 1.0f) {
        for (int y = 0; y < m_scaledHeight; ++y) {
            for (int x = 0; x < m_scaledWidth; ++x) {
                int srcX = x / m_config.scale;
                int srcY = y / m_config.scale;
                const unsigned char* src_pixel = m_frame.data + srcY * m_frame.stride + srcX * 4;
                unsigned char* dst_pixel = m_scaledBuffer.data() + y * m_scaledWidth * 4 + x * 4;
                memcpy(dst_pixel, src_pixel, 4);
            }
        }
        m_frame.width = m_scaledWidth;
        m_frame.height = m_scaledHeight;
        m_frame.stride = m_scaledWidth * 4;
        m_frame.format = PixelFormat::BGRA32;
        m_frame.data = m_scaledBuffer.data();
    }

    return m_frame;
}