#define NOMINMAX
#include "server_context.h"
#include "packet_handlers.h"
#include "platform.h"
#include "utils.h"
#include "packet_codec.h"
#include "http_streamer.h"
#include "differential_streamer.h"
#ifdef USE_FFMPEG
#include "avcodec_streamer.h"
#endif
#include <algorithm>
#include <sstream>
#include "special_keycodes.h"

static const char* TAG = "PacketHandlers";
//=============================================================================
// Packet Handler Functions
//=============================================================================

void handleConfigPacket(ServerContext& ctx, Platform* platform, const std::string& clientKey, const char* payload, size_t payloadSize) {
    std::vector<int> buttonCodes;
    if (!Controller::ParseConfigPacket(payload, payloadSize, buttonCodes)) {
        if (ctx.verbose) {
            LOGW(TAG, "CONFIG from " + clientKey + ": parse failed. Invalid size: " + std::to_string(payloadSize) + " bytes");
        }
        return;
    }
    
    std::lock_guard<std::mutex> lock(ctx.clientsMutex);
    ClientConfig& config = ctx.clients[clientKey];
    
    config.buttonCodes = buttonCodes;
    config.currentDuty.assign(buttonCodes.size(), 0.0);
    config.lastUpdate = platform->GetTickCountMs();

    if (ctx.serverType == ServerType::RETROARCH && config.port == -1) {
        // Find first free port
        for (int i = 0; i < 16; ++i) {
            if (!ctx.assignedPorts[i]) {
                config.port = i;
                ctx.assignedPorts[i] = true;
                if (ctx.verbose) LOGI(TAG, "Assigned port " + std::to_string(i) + " to client " + clientKey);
                break;
            }
        }
    } else if (ctx.serverType == ServerType::STANDARD) {
        config.port = 0;
    }
    
    if (ctx.verbose) {
        std::string buttonList;
        for (int code : buttonCodes) { buttonList += " " + std::to_string(code); }
        LOGI(TAG, "CONFIG from " + clientKey + ": port=" + std::to_string(config.port) + ", " + std::to_string(buttonCodes.size()) + " buttons" + buttonList);
    }
}

void handleStatePacket(ServerContext& ctx, Platform* platform, const std::string& clientKey, const char* payload, size_t payloadSize) {
    std::lock_guard<std::mutex> lock(ctx.clientsMutex);
    
    auto it = ctx.clients.find(clientKey);
    if (it == ctx.clients.end()) return;
    
    ClientConfig& config = it->second;
    std::vector<float> sensorValues;
    if (!Controller::ParseStatePacket(payload, payloadSize, config.buttonCodes.size(), sensorValues)) {
        if (ctx.verbose) {
            LOGW(TAG, "STATE from " + clientKey + ": parse failed. Expected " + std::to_string(config.buttonCodes.size()) + " floats, but payload size was " + std::to_string(payloadSize) + " bytes");
        }
        return;
    }
    
    // Update duty cycle values (processing happens in accumulator thread)
    for (size_t i = 0; i < sensorValues.size(); ++i) {
        // For regular buttons, clamp duty cycle between 0.0 and 1.0.
        // For special mouse movement codes, pass the value through directly.
        int vk = config.buttonCodes[i];
        if (IsSpecialMouseCode(vk)) {
            config.currentDuty[i] = (double)sensorValues[i];
        } else {
            if (sensorValues[i] > 1) sensorValues[i] = 1.0f;
            if (sensorValues[i] < -1) sensorValues[i] = -1.0f;
            config.currentDuty[i] = sensorValues[i];
        }
    }
    
    config.lastUpdate = platform->GetTickCountMs();
    
    // Signal accumulator thread that new state arrived
    platform->SetEvent(ctx.stateUpdateEvent);
    
    if (ctx.verbose) {
        std::string valueList;
        for (float val : sensorValues) { valueList += " " + std::to_string(val); }
        LOGD(TAG, "STATE from " + clientKey + ":" + valueList);
    }
}

void handleMousePacket(ServerContext& ctx, IInputInjector* inputInjector, const std::string& clientKey, const char* payload, size_t payloadSize) {
    MousePacket mouseData;
    if (!Controller::ParseMousePacket(payload, payloadSize, mouseData)) {
        if (ctx.verbose) {
            LOGW(TAG, "MOUSE from " + clientKey + ": parse failed. Invalid size: " + std::to_string(payloadSize) + " bytes");
        }
        return;
    }

    // Update shared memory
    ctx.sharedInput->mouse.x = static_cast<int32_t>(mouseData.x);
    ctx.sharedInput->mouse.y = static_cast<int32_t>(mouseData.y);
    ctx.sharedInput->mouse.wheel = static_cast<int32_t>(mouseData.wheel);
    ctx.sharedInput->mouse.buttons = mouseData.buttons;
    ctx.sharedInput->mouse.lastUpdateMillis = 0; // This is set by the accumulator thread
    
    // Send input immediately
    if (inputInjector->IsAvailable()) {
        auto it = ctx.clients.find(clientKey);
        if (it == ctx.clients.end()) return;
        ClientConfig& config = it->second;
        int port = config.port;
        PortState& state = ctx.portStates[port];

        // Mouse movement
        if (ctx.mouseMode == MouseMode::ABSOLUTE_MODE) {
            if (mouseData.x != state.prevMouseX || mouseData.y != state.prevMouseY) {
                inputInjector->SendMouseMoveAbsolute(mouseData.x, mouseData.y, port);
                state.prevMouseX = mouseData.x;
                state.prevMouseY = mouseData.y;
            }
        } else {
            int32_t deltaX = mouseData.x - state.prevMouseX;
            int32_t deltaY = mouseData.y - state.prevMouseY;
            if (deltaX != 0 || deltaY != 0) {
                inputInjector->SendMouseMove(deltaX, deltaY, port);
                state.prevMouseX = mouseData.x;
                state.prevMouseY = mouseData.y;
            }
        }
        
        // Mouse buttons
        uint32_t mouseChanged = state.prevMouseButtons ^ mouseData.buttons;
        if (mouseChanged) {
            for (int i = 0; i < 5; ++i) {
                if (mouseChanged & (1u << i)) {
                    bool pressed = (mouseData.buttons & (1u << i)) != 0;
                    if (pressed) {
                        inputInjector->SendMouseButtonDown(i, port);
                    } else {
                        inputInjector->SendMouseButtonUp(i, port);
                    }
                }
            }
            state.prevMouseButtons = mouseData.buttons;
        }
        
        // Mouse wheel
        if (mouseData.wheel != state.prevMouseWheel) {
            int32_t wheelDelta = mouseData.wheel - state.prevMouseWheel;
            if (wheelDelta != 0) {
                inputInjector->SendMouseWheel(wheelDelta, port);
            }
            state.prevMouseWheel = mouseData.wheel;
        }
    }
    
    if (ctx.verbose) {
        std::stringstream ss;
        ss << "MOUSE: pos(" << mouseData.x << "," << mouseData.y 
           << ") wheel=" << mouseData.wheel 
           << " buttons=0x" << std::hex << mouseData.buttons << std::dec;
        LOGD(TAG, ss.str());
    }
}

void handleXInputPacket(ServerContext& ctx, Platform* platform, const char* payload, size_t payloadSize) {
    XInputPacket xinputData;
    if (!Controller::ParseXInputPacket(payload, payloadSize, xinputData)) {
        if (ctx.verbose) {
            LOGW(TAG, "XINPUT: parse failed. Invalid size: " + std::to_string(payloadSize) + " bytes");
        }
        return;
    }
    
    if (xinputData.controllerIndex >= 4) return;
    
    uint8_t idx = xinputData.controllerIndex;
    
    // Mark controller as connected
    ctx.sharedInput->xi.connectedMask |= (1u << idx);
    
    // Convert floating point values to XInput native ranges
    ctx.sharedInput->xi.controllers[idx].buttons = xinputData.buttons;
    ctx.sharedInput->xi.controllers[idx].leftTrigger = static_cast<uint8_t>(xinputData.leftTrigger * 255);
    ctx.sharedInput->xi.controllers[idx].rightTrigger = static_cast<uint8_t>(xinputData.rightTrigger * 255);
    ctx.sharedInput->xi.controllers[idx].sThumbLX = static_cast<int16_t>(xinputData.thumbLX * 32767);
    ctx.sharedInput->xi.controllers[idx].sThumbLY = static_cast<int16_t>(xinputData.thumbLY * 32767);
    ctx.sharedInput->xi.controllers[idx].sThumbRX = static_cast<int16_t>(xinputData.thumbRX * 32767);
    ctx.sharedInput->xi.controllers[idx].sThumbRY = static_cast<int16_t>(xinputData.thumbRY * 32767);
    
    ctx.sharedInput->xi.lastUpdateMillis = platform->GetTickCountMs();
    
    if (ctx.verbose) {
        std::stringstream ss;
        ss << "XINPUT[" << (int)idx << "]: buttons=0x" << std::hex << xinputData.buttons << std::dec
           << " LT=" << xinputData.leftTrigger << " RT=" << xinputData.rightTrigger
           << " L(" << xinputData.thumbLX << "," << xinputData.thumbLY << ")"
           << " R(" << xinputData.thumbRX << "," << xinputData.thumbRY << ")";
        LOGD(TAG, ss.str());
    }
}

void handleScreenConfigPacket(ServerContext& ctx, Platform* platform, const char* payload, size_t payloadSize) {
    LOGD(TAG, "Handling screen packet");
    ScreenConfigPacket config;
    if (!Controller::ParseScreenConfigPacket(payload, payloadSize, config)) {
        if (ctx.verbose) {
            LOGW(TAG, "SCREEN_CONFIG: parse failed. Invalid size: " + std::to_string(payloadSize) + " bytes");
        }
        return;
    }
    LOGD(TAG, "Parsing successful");

    std::lock_guard<std::mutex> lock(ctx.screenServerMutex);

    // Build configs
    CaptureConfig captureConfig;
    StreamerConfig streamerConfig;

    streamerConfig.fps = config.fps;
    streamerConfig.bitrate = config.bitrate * 1000; // convert kbps to bps
    streamerConfig.adaptiveQuality.quality = config.quality;
    streamerConfig.adaptiveQuality.minQuality = config.minQuality;
    streamerConfig.maxClientBufferSize = static_cast<int>(config.clientBuffer) * 1024; // KB -> bytes
    streamerConfig.captureScreenWithCursor = config.cursorEnabled;
    streamerConfig.streamingMode = (config.streamingMode == 1) ? STREAMING_DIFFERENTIAL : (config.streamingMode == 2 ? STREAMING_H264 : STREAMING_MJPEG);
    streamerConfig.scale = config.scale;
    // Prefer the existing streamer's bound port, fallback to stored ctx.screenPort (may be 0 for ephemeral)
    if (ctx.screenStreamer) {
        streamerConfig.port = ctx.screenStreamer->GetPort();
    } else {
        streamerConfig.port = ctx.screenPort; // 0 means request ephemeral
    }

    captureConfig.scale = config.scale;
    captureConfig.method = (config.method == 1) ? CAPTURE_DESKTOP_DUPLICATION : CAPTURE_GDI;

    // Use DI-provided resources from ServerContext
    IImageEncoder* imageEncoder = ctx.screenImageEncoder;
    IScreenCapture* capture = ctx.screenCapture;
    if (!imageEncoder || !capture) {
        LOGE(TAG, "No injected ImageEncoder or ScreenCapture available");
        return;
    }

    // Try to apply new configuration to an existing streamer when possible
    if (ctx.screenStreamer) {
        // If streaming mode changed we must recreate; otherwise try to update in-place.
        if (ctx.screenStreamer->GetStreamingMode() == streamerConfig.streamingMode) {
            if (ctx.screenStreamer->UpdateConfig(streamerConfig, captureConfig)) {
                LOGI(TAG, "Updated existing in-process screen streamer configuration in-place");
                return;
            } else {
                LOGI(TAG, "Configuration requires restart (port change or capture init failed). Recreating streamer");
                uint16_t preservedPort = ctx.screenStreamer->GetPort();
                // Preserve the currently bound port so it can be reused.
                ctx.screenStreamer->Stop();
                delete ctx.screenStreamer;
                ctx.screenStreamer = nullptr;
                // Fall through to recreate logic
                streamerConfig.port = preservedPort;
            }
        } else {
            LOGI(TAG, "Streaming mode changed, recreating streamer");
            uint16_t preservedPort = ctx.screenStreamer->GetPort();
            // Preserve the currently bound port if present so we reuse it for the new streamer.
            ctx.screenStreamer->Stop();
            delete ctx.screenStreamer;
            ctx.screenStreamer = nullptr;
            streamerConfig.port = preservedPort;
        }
    }

    // Ensure we can initialize capture before creating a new streamer
    if (!capture->Initialize(captureConfig)) {
        LOGE(TAG, "Failed to initialize screen capture");
        return;
    }

    BaseStreamer* streamer = nullptr;
    if (streamerConfig.streamingMode == STREAMING_MJPEG) {
        streamer = new HTTPStreamer(streamerConfig, capture, platform, imageEncoder);
    } else if (streamerConfig.streamingMode == STREAMING_H264) {
#ifdef USE_FFMPEG
        streamer = new AvcodecStreamer(streamerConfig, capture, platform, imageEncoder);
#else
        LOGE(TAG, "H264 streaming not supported in this build. Falling back to MJPEG.");
        streamer = new HTTPStreamer(streamerConfig, capture, platform, imageEncoder);
#endif
    } else {
        streamer = new DifferentialStreamer(streamerConfig, capture, platform, imageEncoder);
    }

    // Store ownership of streamer in context so we can stop it later
    ctx.screenStreamer = streamer;

    // Start the streamer immediately.
    if (!ctx.screenStreamer->Start()) {
        LOGE(TAG, "Failed to start screen streamer");
        delete ctx.screenStreamer;
        ctx.screenStreamer = nullptr;
        return;
    }

    // Record the actual bound port in the context so future updates reuse it.
    ctx.screenPort = ctx.screenStreamer->GetPort();

    LOGI(TAG, "Screen server started in-process on port " + std::to_string(ctx.screenStreamer->GetPort()));
}

void handleScreenStopPacket(ServerContext& ctx, Platform* platform) {
    std::lock_guard<std::mutex> lock(ctx.screenServerMutex);

    if (ctx.screenStreamer) {
        LOGI(TAG, "Stopping in-process screen server...");
        ctx.screenStreamer->Stop();
        delete ctx.screenStreamer;
        ctx.screenStreamer = nullptr;
        ctx.screenPort = 0;
        LOGI(TAG, "Screen server stopped");
    } else if (ctx.screenServerProcess != nullptr && platform->TerminateProcess(ctx.screenServerProcess, 0)) {
        // Fallback to legacy process-based stop behavior
        platform->CloseProcessHandle(ctx.screenServerProcess);
        ctx.screenServerProcess = nullptr;
        LOGI(TAG, "Screen server process stopped");
    }
}

void handleTextInputPacket(ServerContext& ctx, IInputInjector* inputInjector, const std::string& clientKey, const char* payload, size_t payloadSize) {
    TextInputPacket textData;
    if (!Controller::ParseTextInputPacket(payload, payloadSize, textData)) {
        if (ctx.verbose) {
            LOGW(TAG, "TEXT from " + clientKey + ": parse failed. Invalid size: " + std::to_string(payloadSize) + " bytes");
        }
        return;
    }
    
    const char* text = payload + sizeof(TextInputPacket);
    
    if (inputInjector->IsAvailable()) {
        inputInjector->SendTextInput(text, textData.length, false);
    }
    
    if (ctx.verbose) {
        LOGD(TAG, "TEXT from " + clientKey + ": \"" + std::string(text, textData.length) + "\"");
    }
}

void handleCommandPacket(ServerContext& ctx, Platform* platform, const std::string& clientKey, const char* payload, size_t payloadSize) {
    CommandPacket commandData;
    if (!Controller::ParseCommandPacket(payload, payloadSize, commandData)) {
        if (ctx.verbose) {
            LOGW(TAG, "COMMAND from " + clientKey + ": parse failed. Invalid size: " + std::to_string(payloadSize) + " bytes");
        }
        return;
    }
    
    const char* command = payload + sizeof(CommandPacket);
    std::string commandStr(command, commandData.length);
    
    if (ctx.verbose) {
        LOGI(TAG, "COMMAND from " + clientKey + ": \"" + commandStr + "\"");
    }
    
    // Parse command to separate executable and parameters
    std::string finalCommand;
    size_t spacePos = commandStr.find(' ');
    std::string executablePart = (spacePos != std::string::npos) ? commandStr.substr(0, spacePos) : commandStr;
    std::string paramsPart = (spacePos != std::string::npos) ? commandStr.substr(spacePos) : "";
    
    size_t lastSlash = executablePart.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        std::string directory = executablePart.substr(0, lastSlash);
        std::string filename = executablePart.substr(lastSlash + 1);
        finalCommand = "cmd.exe /c start \"\" /D \"" + directory + "\" \"" + filename + "\"" + paramsPart;
    } else {
        finalCommand = "cmd.exe /c start \"\" " + commandStr;
    }
    platform->CreateNewProcess(finalCommand);
}

void handleKeycodeDownPacket(ServerContext& ctx, IInputInjector* inputInjector, const std::string& clientKey, const char* payload, size_t payloadSize) {
    KeycodePacket keycodeData;
    if (!Controller::ParseKeycodeDownPacket(payload, payloadSize, keycodeData)) {
        if (ctx.verbose) {
            LOGW(TAG, "KEYCODE_DOWN from " + clientKey + ": parse failed. Invalid size: " + std::to_string(payloadSize) + " bytes");
        }
        return;
    }
    
    const char* buffer = payload + sizeof(KeycodePacket);
    
    std::lock_guard<std::mutex> lock(ctx.explicitKeysMutex);
    for (uint16_t i = 0; i < keycodeData.count; ++i) {
        uint32_t keycode = ReadInt32(buffer);
        buffer += sizeof(uint32_t);
        ctx.explicitKeyStates[keycode] = true;
    }
    
    if (ctx.verbose) {
        LOGD(TAG, "KEYCODE_DOWN from " + clientKey + ": " + std::to_string(keycodeData.count) + " keys");
    }
}

void handleKeycodeUpPacket(ServerContext& ctx, IInputInjector* inputInjector, const std::string& clientKey, const char* payload, size_t payloadSize) {
    KeycodePacket keycodeData;
    if (!Controller::ParseKeycodeUpPacket(payload, payloadSize, keycodeData)) {
        if (ctx.verbose) {
            LOGW(TAG, "KEYCODE_UP from " + clientKey + ": parse failed. Invalid size: " + std::to_string(payloadSize) + " bytes");
        }
        return;
    }
    
    const char* buffer = payload + sizeof(KeycodePacket);
    
    std::lock_guard<std::mutex> lock(ctx.explicitKeysMutex);
    for (uint16_t i = 0; i < keycodeData.count; ++i) {
        uint32_t keycode = ReadInt32(buffer);
        buffer += sizeof(uint32_t);
        ctx.explicitKeyStates[keycode] = false;
    }
    
    if (ctx.verbose) {
        LOGD(TAG, "KEYCODE_UP from " + clientKey + ": " + std::to_string(keycodeData.count) + " keys");
    }
}

void handleMouseConfigPacket(ServerContext& ctx, const char* payload, size_t payloadSize) {
    if (payloadSize >= 1) {
        std::lock_guard<std::mutex> lock(ctx.clientsMutex);
        ctx.mouseMode = static_cast<MouseMode>(payload[0]);
        if (ctx.verbose) {
            const char* modeStr = (ctx.mouseMode == MouseMode::RELATIVE_MODE) ? "relative" :
                                (ctx.mouseMode == MouseMode::ABSOLUTE_MODE) ? "absolute" : "touchscreen";
            LOGI(TAG, "Mouse mode: " + std::string(modeStr));
        }
    }
}