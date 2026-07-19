#include "differential_streamer.h"
#include "bitmap_differ.h"
#include "video_config.h"
#include "screenshare_protocol.h"
#include "platform.h"
#include "utils.h"
#include "rect.h"
#include <iomanip> // For std::setprecision
#include <vector>
#include <sstream>
#include <string>
#include <atomic>

static const char* TAG = "DifferentialStreamer";
const int FULL_FRAME_INTERVAL = 120; // Send a full frame every N frames to prevent artifacting.

DifferentialStreamer::DifferentialStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder)
    : TcpStreamer(config, capture, platform, imageEncoder),
      m_lastX(-1), m_lastY(-1), m_lastVisibility(false), m_lastCursorHash(0),
      m_streamFrameCount(0),
      m_totalCaptureTimeMs(0.0),
      m_totalDiffTimeMs(0.0),
      m_totalEncodeTimeMs(0.0),
      m_totalCursorTimeMs(0.0)
{
    m_bitmapDiffer = new BitmapDiffer();
}

DifferentialStreamer::~DifferentialStreamer() {
    delete m_bitmapDiffer;
}

void DifferentialStreamer::HandleClient(uintptr_t client) {
    m_platform->SetSocketNonBlocking(client, true);
    m_platform->SetSocketSendBuffer(client, m_config.tcpBufferSize);
    
    std::lock_guard<std::mutex> lock(m_clientsLock);
    if (m_clientCount < 10) {
        m_clients[m_clientCount].socket = client;
        m_clients[m_clientCount].sendBuffer.clear();
        m_clients[m_clientCount].sendBuffer.reserve(m_config.maxClientBufferSize);
        m_clientCount++;
        LOGI(TAG, "Differential client connected. Total clients: " + std::to_string(m_clientCount));
    } else {
        m_platform->CloseSocket(client);
    }
    // Client thread's job is done. The main streaming loop handles all I/O.
}

bool DifferentialStreamer::CaptureAndEncode(int quality) {
    std::vector<char> outPacket;
    uint64_t start_time, end_time;
    start_time = m_platform->GetTickCountMs();

    // Periodically force a full frame to correct any accumulated artifacts
    // and ensure stream stability.
    if (m_streamFrameCount > 0 && (m_streamFrameCount % FULL_FRAME_INTERVAL == 0 || isKeyframeRequested())) {
        m_bitmapDiffer->Reset();
        m_streamFrameCount = 0; // Reset counter to avoid overflow and keep logic simple
        LOGD(TAG, "Forcing full frame refresh");
    }
    m_streamFrameCount++;

    // In differential streaming, we capture the frame without the cursor first,
    // because the cursor is handled as a separate packet.
    ImageUtils::RawImageFrame frame = m_capture->CaptureFrame(false);

    end_time = m_platform->GetTickCountMs();
    uint64_t capture_duration = end_time - start_time;
    m_totalCaptureTimeMs += capture_duration;

    if (frame.data) {
        start_time = m_platform->GetTickCountMs();
        DiffResult diff;
        if (m_achievedFps > 10.0) {
            diff.type = Screenshare::FRAME_TYPE_FULL;
            diff.x = 0;
            diff.y = 0;
            diff.width = frame.width;
            diff.height = frame.height;
            diff.hasChanges = true;
        } else {
            diff = m_bitmapDiffer->Compare(frame);
        }
        end_time = m_platform->GetTickCountMs();
        uint64_t diff_duration = end_time - start_time;

        m_totalDiffTimeMs += diff_duration;
        if (diff.hasChanges) {
            unsigned char* jpegData = nullptr;
            size_t jpegSize = 0;
            bool success = false;
            
            start_time = m_platform->GetTickCountMs();
            if (diff.type == Screenshare::FRAME_TYPE_FULL) { // Full frame
                success = m_imageEncoder->EncodeToJPEG(frame, &jpegData, &jpegSize, quality, nullptr);
            } else { // Partial frame
                Utils::Rect cropRect(diff.x, diff.y, diff.x + diff.width, diff.y + diff.height);
                success = m_imageEncoder->EncodeToJPEG(frame, &jpegData, &jpegSize, quality, &cropRect);
            }
            
            end_time = m_platform->GetTickCountMs();
            uint64_t encode_duration = end_time - start_time;
            m_totalEncodeTimeMs += encode_duration;

            if (success && jpegData) {
                // Dump frame for debugging
                // char debug_filename[256];
                // sprintf(debug_filename, "diff_frame_t%d_x%d_y%d_w%d_h%d.jpg",
                //         diff.type, diff.x, diff.y, diff.width, diff.height);
                // DumpDataToFile(debug_filename, jpegData, jpegSize);

                // --- Build Binary Frame Packet ---
                EncodeFramePacket(outPacket, diff, jpegData, jpegSize);
            }
        }
    }
    
    if (m_config.captureScreenWithCursor) {
        start_time = m_platform->GetTickCountMs();
        BuildCursorPacket(m_capture, outPacket);
        end_time = m_platform->GetTickCountMs();
        uint64_t cursor_duration = end_time - start_time;
        m_totalCursorTimeMs += cursor_duration;
    }
    
    setKeyframeRequested(false); // Reset flag

    m_framesSincePerfLog++;

    // Update child metrics into shared map; base LogPerformanceMetrics will log everything together
    if (m_framesSincePerfLog > 0) {
        auto fmt = [](double v) { std::ostringstream o; o << std::fixed << std::setprecision(1) << v; return o.str(); };
        double n = m_framesSincePerfLog;
        g_perfMetrics["Capture ms"] = fmt(m_totalCaptureTimeMs / n);
        g_perfMetrics["Diff ms"]     = fmt(m_totalDiffTimeMs / n);
        g_perfMetrics["Encode ms"]   = fmt(m_totalEncodeTimeMs / n);
        g_perfMetrics["Cursor ms"]   = fmt(m_totalCursorTimeMs / n);
    }

    if (outPacket.empty())
        return false;
    
    QueuePacketToTcpClientSendBuffers(outPacket, m_platform->GetTickCountMs());
    return true;
}

bool DifferentialStreamer::BuildCursorPacket(IScreenCapture* capture, std::vector<char>& outPacket) {
    CursorFrame cursorFrame = capture->GetCursorFrame();

    bool positionChanged = (cursorFrame.x != m_lastX || cursorFrame.y != m_lastY);
    bool visibilityChanged = (cursorFrame.isVisible != m_lastVisibility);
    uint32_t currentHash = 0;
    bool imageChanged = false;

    if (cursorFrame.isVisible && cursorFrame.image.data) {
        currentHash = HashData(cursorFrame.image.data, cursorFrame.image.stride * cursorFrame.image.height, 37);
        imageChanged = (currentHash != m_lastCursorHash);
    } else if (!cursorFrame.isVisible && m_lastVisibility) {
        // If the cursor just disappeared, we need to send an update.
        // The hash will be 0, which is different from the previous hash.
        imageChanged = (0 != m_lastCursorHash);
    }

    if (!positionChanged && !visibilityChanged && !imageChanged && !isKeyframeRequested()) {
        return false;
    }

    // Update last known state regardless of whether we send a packet.
    // This prevents missing positional changes.
    m_lastX = cursorFrame.x;
    m_lastY = cursorFrame.y;
    m_lastVisibility = cursorFrame.isVisible;
    m_lastCursorHash = currentHash;

    unsigned char* pngData = nullptr;
    size_t pngSize = 0;

    // Only encode the image if it has actually changed and is visible
    if (isKeyframeRequested() || (imageChanged && cursorFrame.isVisible)) {
        if (!m_imageEncoder->EncodeToPNG(cursorFrame.image, &pngData, &pngSize)) {
            // If encoding fails, ensure any allocated pngData is freed before returning.
            if (pngData) {
                delete[] pngData;
                pngData = nullptr;
                pngSize = 0;
            }
            // Encoding failed, abort.
            return false;
        }
    }

    // Use diff.type = 3 for cursor packets
    // The packet now includes visibility status.
    // if (pngData && pngSize > 0) {
    //     char debug_filename[256];
    //     sprintf(debug_filename, "cursor_x%d_y%d_w%d_h%d.png", cursorFrame.x, cursorFrame.y, cursorFrame.image.width, cursorFrame.image.height);
    //     DumpDataToFile(debug_filename, pngData, pngSize);
    // }

    // --- Build Binary Cursor Packet ---
    EncodeCursorPacket(outPacket, cursorFrame, pngData, pngSize);

    return true;
}

void DifferentialStreamer::EncodeFramePacket(std::vector<char>& outPacket, const DiffResult& diff, const unsigned char* jpegData, size_t jpegSize) {
    // Packet Type (1 byte: 1 for frame)
    outPacket.push_back(Screenshare::PACKET_TYPE_FRAME);
    // Frame Type (1 byte: 1 for diff, 2 for full)
    outPacket.push_back(static_cast<char>(diff.type)); // diff.type is already ScreenshareProtocol::FrameType
    // Coordinates and Size (4 bytes each)
    AppendInt32(outPacket, diff.x);
    AppendInt32(outPacket, diff.y);
    AppendInt32(outPacket, diff.width);
    AppendInt32(outPacket, diff.height);

    // JPEG Data Size (4 bytes)
    AppendInt32(outPacket, static_cast<int32_t>(jpegSize));

    // JPEG Data
    if (jpegData && jpegSize > 0) {
        outPacket.insert(outPacket.end(), jpegData, jpegData + jpegSize);
    }
}

void DifferentialStreamer::EncodeCursorPacket(std::vector<char>& outPacket, const CursorFrame& cursorFrame, unsigned char* pngData, size_t pngSize) {
    // Packet Type (1 byte: 3 for cursor)
    outPacket.push_back(Screenshare::PACKET_TYPE_CURSOR);
    // isMonochrome (1 byte: 1 for true, 0 for false)
    outPacket.push_back(cursorFrame.isMonochrome ? 1 : 0);
    // Visibility (1 byte: 1 for visible, 0 for hidden)
    outPacket.push_back(cursorFrame.isVisible ? 1 : 0);

    // Coordinates (4 bytes each)
    AppendInt32(outPacket, cursorFrame.x);
    AppendInt32(outPacket, cursorFrame.y);

    // PNG Data Size (4 bytes)
    AppendInt32(outPacket, static_cast<int32_t>(pngSize));

    // PNG Data (if any)
    if (pngData && pngSize > 0) {
        outPacket.insert(outPacket.end(), pngData, pngData + pngSize);
    }
}
