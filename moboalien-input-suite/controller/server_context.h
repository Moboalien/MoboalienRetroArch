#pragma once
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include "platform.h"
#include "shared_input.h"

// Forward declarations — full headers only needed by the Windows screen-streaming
// implementation, not by the controller core or Android build.
class BaseStreamer;
class IScreenCapture;
class IImageEncoder;

// Forward declaration
struct ClientConfig;

enum class MouseMode : uint8_t {
    RELATIVE_MODE = 0,
    ABSOLUTE_MODE = 1,
    TOUCHSCREEN_MODE = 2
};

enum class ServerType : uint8_t {
    STANDARD = 0,
    RETROARCH = 1
};

struct KeyState {
    double accumulator;
    bool isPressed;
    uint64_t lastUpdate;
    uint64_t pressStartTime;

    KeyState() : accumulator(0.0), isPressed(false), lastUpdate(0), pressStartTime(0) {}
};

struct PortState {
    std::unordered_map<int, KeyState> perKeyState;
    std::unordered_map<int, bool> overallKeyStates;
    int mouseAnchorX = 0, mouseAnchorY = 0;
    int prevMouseX = 0, prevMouseY = 0;
    uint32_t prevMouseButtons = 0;
    int32_t prevMouseWheel = 0;
};

// Server context - contains all shared state
struct ServerContext {
    SharedInput* sharedInput;
    std::unordered_map<std::string, ClientConfig> clients;
    std::mutex clientsMutex;
    bool verbose;
    std::unordered_map<int, PortState> portStates;
    std::unordered_map<int, bool> explicitKeyStates;
    std::mutex explicitKeysMutex;

    ServerType serverType = ServerType::STANDARD;
    bool assignedPorts[16] = {false};

    // Global mouse mode
    MouseMode mouseMode = MouseMode::RELATIVE_MODE;

    // Screen server — raw pointer, owned and deleted by packet_handlers (Windows only)
    Platform::ProcessHandle screenServerProcess;
    BaseStreamer* screenStreamer = nullptr;
    IScreenCapture* screenCapture = nullptr;
    IImageEncoder* screenImageEncoder = nullptr;
    std::mutex screenServerMutex;

    // The actual port the in-process screen server is bound to (0 if none / ephemeral)
    uint16_t screenPort = 0;

    // Event for waking accumulator thread
    Platform::EventHandle stateUpdateEvent;

    // Configuration
    int minPressDurationMs;
};

// Client configuration structure
struct ClientConfig {
    std::vector<int> buttonCodes;
    std::vector<double> currentDuty;
    uint64_t lastUpdate;
    int port = -1;
};

struct MouseClientState {
    float prevX, prevY, prevWheel;
    uint32_t prevButtons;
};
