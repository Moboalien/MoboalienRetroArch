#pragma once
#include "base_streamer.h"
#include <vector>
#include <mutex>
#include <string>

class UdpStreamer : public BaseStreamer {
public:
    UdpStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder);
    virtual ~UdpStreamer();

    bool Start() override;
    void Stop() override;
    int GetCongestedClientCount() const override { return 0; } // UDP has no backpressure

protected:
    struct UdpClient {
        std::string ip;
        int port;
        bool active;
        uint64_t lastHeartbeat;
    };

    // Removes timed-out clients. Must be called with m_udpClientsLock held.
    void EvictTimedOutClients();

    std::vector<UdpClient> m_udpClients;
    mutable std::mutex m_udpClientsLock;
    uint32_t m_frameId = 0;
};
