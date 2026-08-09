#pragma once

#include <memory>
#include <string>
#include <vector>
#include "platform.h"
#include "packet_encryptor.h"
#include "i_input_injector.h"

class IImageEncoder;
class IScreenCapture;

enum class ServerType : uint8_t;

class IController {
public:
    virtual ~IController() = default;

    virtual void Run() = 0;
    virtual void SetMinPressDuration(int ms) = 0;

    // Return the ports the controller is using (may be chosen by system)
    virtual uint16_t GetControllerPort() const = 0;
    virtual uint16_t GetScreenPort() const = 0;
};

// Factory function to create a controller instance
std::unique_ptr<IController> CreateController(Platform* platform, IInputInjector* inputInjector, bool verbose, ServerType serverType, PacketEncryptor* packetEncryptor = nullptr, IImageEncoder* imageEncoder = nullptr, IScreenCapture* screenCapture = nullptr);