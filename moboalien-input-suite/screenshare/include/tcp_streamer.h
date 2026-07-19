#pragma once
#include "base_streamer.h"
#include "circular_buffer.h"
#include <mutex>
#include <vector>

class TcpStreamer : public BaseStreamer {
public:
    TcpStreamer(const StreamerConfig& config, IScreenCapture* capture, Platform* platform, IImageEncoder* imageEncoder);
    virtual ~TcpStreamer();

    bool Start() override;
    void Stop() override;
    int GetCongestedClientCount() const override;

    void HandleClientPublic(uintptr_t client);

protected:
    struct ClientState {
        uintptr_t socket;
        CircularBuffer sendBuffer;
        uint64_t bufferFullSince = 0;
        ClientState() : socket(Platform::INVALID_SOCKET_HANDLE) {}
    };

    virtual void HandleClient(uintptr_t client) = 0;

    void ServerLoop() override;
    size_t FlushSendBuffers() override;
    void OnBeforeFrame() override { ProcessClientRequestsIfAny(); }
    void FlushLoop();
    void QueuePacketToTcpClientSendBuffers(const std::vector<char>& packet, uint64_t now);
    void RemoveClient(int index);
    void ProcessClientRequestsIfAny();

    ClientState m_clients[10];
    mutable std::mutex m_clientsLock;
    Platform::ThreadHandle m_flushThread;

private:
    static void* FlushThreadProc(void* param);
};
