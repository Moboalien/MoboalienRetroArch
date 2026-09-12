#pragma once

#include "server_context.h"
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>
#include "platform.h"
#include "i_input_injector.h"
#include "controller/i_controller.h"
#include "i_crypto_helper.h"
#include "packet_encryptor.h"

class Controller final : public IController {
public:
    Controller(Platform* platform, IInputInjector* inputInjector, bool verbose, ServerType serverType = ServerType::STANDARD, PacketEncryptor* packetEncryptor = nullptr, IImageEncoder* imageEncoder = nullptr, IScreenCapture* screenCapture = nullptr, uint16_t controllerPort = 0, uint16_t screenPort = 0, bool enableScreenStreaming = true);
    ~Controller();

    void Run() override;
    void Stop() override;
    void SetMinPressDuration(int ms) override;

    // IController port accessors
    uint16_t GetControllerPort() const override;
    uint16_t GetScreenPort() const override;

private:
    void accumulatorThreadFunction();
    static void* accumulatorThreadEntry(void* context);

    void ProcessOverallKeyChanges(std::unordered_map<int, bool>& oldStates, const std::unordered_map<int, bool>& newStates, int port);
    void ProcessMouseMovement(const std::unordered_map<int, double>& mouseMovementValues, int port);
    void ProcessJoystickMovement(const std::unordered_map<int, double>& joystickValues, int port);
    bool isTouchScreenMode(double x, double y) const {
        return m_context.mouseMode == MouseMode::TOUCHSCREEN_MODE || (x > 0.0f && x < 1.0f && y > 0.0f && y < 1.0f);
    };
    Platform::SharedMemoryHandle initializeSharedMemory(SharedInput** outSharedInput);

    void handlePacket(std::vector<uint8_t> &decryptedData, const std::string& clientKey, uint16_t clientPort);
    std::vector<uint8_t> decryptPacket(const char* buffer, int size, bool& outIsValid);

    Platform* m_platform;
    IInputInjector* m_inputInjector;
    PacketEncryptor* m_packetEncryptor;
    ServerContext m_context;
    uintptr_t m_udpSocket;
    Platform::SharedMemoryHandle m_sharedMemoryHandle;
    Platform::ThreadHandle m_accumulatorThreadHandle;
    bool m_running;
    uint64_t m_shutdownCallbackId = 0;
    uint16_t m_controllerPort;
};