#include "http_streamer.h"
#include "platform.h"
#include "utils.h"
#include <iomanip>
#include <vector>
#include <string>
#include <sstream>

static const char* TAG = "HTTPStreamer";

HTTPStreamer::HTTPStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder)
    : TcpStreamer(config, capture, platform, imageEncoder),
      m_totalCaptureTimeMs(0.0),
      m_totalEncodeTimeMs(0.0)
{}

HTTPStreamer::~HTTPStreamer() {}

void HTTPStreamer::HandleClient(uintptr_t client) {
    SendHttpResponse(client);
    m_platform->SetSocketNonBlocking(client, true);

    std::lock_guard<std::mutex> lock(m_clientsLock);
    if (m_clientCount < 10) {
        m_clients[m_clientCount].socket = client;
        m_clients[m_clientCount].sendBuffer.clear();
        m_clients[m_clientCount].sendBuffer.reserve(m_config.maxClientBufferSize);
        m_clientCount++;
        LOGI(TAG, "HTTP client connected. Total clients: " + std::to_string(m_clientCount));
    } else {
        m_platform->CloseSocket(client);
    }
}

bool HTTPStreamer::CaptureAndEncode(int quality) {
    uint64_t start_time, end_time;
    start_time = m_platform->GetTickCountMs();
    ImageUtils::RawImageFrame frame = m_capture->CaptureFrame(m_config.captureScreenWithCursor);
    end_time = m_platform->GetTickCountMs();
    m_totalCaptureTimeMs += (end_time - start_time);

    if (!frame.data) {
        return false;
    }

    unsigned char* jpegData = nullptr;
    size_t jpegSize = 0;

    start_time = m_platform->GetTickCountMs();
    bool success = m_imageEncoder->EncodeToJPEG(frame, &jpegData, &jpegSize, quality, nullptr);
    end_time = m_platform->GetTickCountMs();
    m_totalEncodeTimeMs += (end_time - start_time);

    std::vector<char> outPacket;
    if (success && jpegData) {
        std::string boundary = "\r\n--boundary\r\n";
        std::stringstream header;
        header << "Content-Type: image/jpeg\r\n";
        header << "Content-Length: " << jpegSize << "\r\n\r\n";
        std::string headerStr = header.str();

        outPacket.insert(outPacket.end(), boundary.begin(), boundary.end());
        outPacket.insert(outPacket.end(), headerStr.begin(), headerStr.end());
        outPacket.insert(outPacket.end(), jpegData, jpegData + jpegSize);

        delete[] jpegData;
    }

    m_framesSincePerfLog++;

    if (m_framesSincePerfLog > 0) {
        auto fmt = [](double v) { std::ostringstream o; o << std::fixed << std::setprecision(1) << v; return o.str(); };
        double n = m_framesSincePerfLog;
        g_perfMetrics["Capture ms"] = fmt(m_totalCaptureTimeMs / n);
        g_perfMetrics["Encode ms"]  = fmt(m_totalEncodeTimeMs / n);
    }

    if (outPacket.empty())
        return false;
    
    QueuePacketToTcpClientSendBuffers(outPacket, m_platform->GetTickCountMs());
    return true;
}

void HTTPStreamer::SendHttpResponse(uintptr_t clientSocket) {
    std::string httpResponse =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=boundary\r\n"
        "Cache-Control: no-cache\r\n"
        "Pragma: no-cache\r\n"
        "Connection: close\r\n\r\n";
    m_platform->Send(clientSocket, httpResponse.c_str(), static_cast<int>(httpResponse.length()), 0);
}