#include "controller.h"
#include "controller/i_controller.h"
#include "packet_handlers.h"
#include "special_keycodes.h"
#include "i_input_injector.h"
#include "config_file.h"
#include "utils.h"
#include "differential_streamer.h"
#include <cstring>
#include "signal_handler.h"
#include "logger.h"

static const char* TAG = "Controller";

Controller::Controller(Platform* platform, IInputInjector* inputInjector, bool verbose, ServerType serverType, PacketEncryptor* packetEncryptor, IImageEncoder* imageEncoder, IScreenCapture* screenCapture, uint16_t controllerPort, uint16_t screenPort, bool enableScreenStreaming)
    : m_platform(platform), m_inputInjector(inputInjector), m_packetEncryptor(packetEncryptor), m_udpSocket(Platform::INVALID_SOCKET_HANDLE), m_sharedMemoryHandle(nullptr), m_accumulatorThreadHandle(nullptr),
      m_controllerPort(controllerPort) {
    m_context.verbose = verbose;
    m_context.serverType = serverType;
    Logger::GetInstance().SetLogLevel(verbose ? LogLevel::VERBOSE : LogLevel::INFO);
    if (m_context.verbose) LOGI(TAG, "Initializing controller...");
    m_context.minPressDurationMs = 20; // Default value
    // Properly initialize all handles and pointers in the context
    m_context.screenServerProcess = nullptr;
    m_context.mouseMode = MouseMode::RELATIVE_MODE;
    m_context.stateUpdateEvent = m_platform->CreateNewEvent();

    // If DI provided encoder/capture, store non-owning pointers in the server context so handlers can use them
    if (imageEncoder) m_context.screenImageEncoder = imageEncoder;
    if (screenCapture) m_context.screenCapture = screenCapture;

    // Initialize ctx.screenPort from constructor argument so initial listeners can use it.
    m_context.screenPort = screenPort;

    // Pre-create and start a DifferentialStreamer if both capture and encoder were injected.
    if (enableScreenStreaming && m_context.screenImageEncoder && m_context.screenCapture) {
        CaptureConfig captureConfig;
        StreamerConfig streamerConfig;
        streamerConfig.streamingMode = STREAMING_DIFFERENTIAL;
        streamerConfig.port = m_context.screenPort;

        if (!m_context.screenCapture->Initialize(captureConfig)) {
            LOGE(TAG, "Failed to initialize screen capture");
        } else {
            m_context.screenStreamer = new DifferentialStreamer(streamerConfig, m_context.screenCapture, m_platform, m_context.screenImageEncoder);
            if (!m_context.screenStreamer->Start()) {
                LOGE(TAG, "Failed to start pre-created DifferentialStreamer");
                delete m_context.screenStreamer;
                m_context.screenStreamer = nullptr;
            } else {
                m_context.screenPort = m_context.screenStreamer->GetPort();
                LOGI(TAG, "Pre-created DifferentialStreamer started on port " + std::to_string(m_context.screenPort));
            }
        }
    }

    m_sharedMemoryHandle = initializeSharedMemory(&m_context.sharedInput);
    if (!m_sharedMemoryHandle) {
        LOGE(TAG, "Couldn't setup shared memory");
        return;
    }

    if (!m_platform->SocketsInitialize()) {
        LOGE(TAG, "SocketsInitialize failed");
        return;
    }

    // If ports were requested as 0, ask the OS for an ephemeral port now so other
    // modules (handshake, etc.) can query them before the controller starts.
    m_udpSocket = m_platform->CreateUDPSocket(m_controllerPort);
    if (m_udpSocket != Platform::INVALID_SOCKET_HANDLE) {
        int allocated = m_platform->GetSocketPort(m_udpSocket);
        if (allocated > 0) {
            m_controllerPort = allocated;
            if (m_context.verbose) LOGI(TAG, "Reserved controller port: " + std::to_string(m_controllerPort));
        }
    }
    else
    {
        LOGE(TAG, "CreateUDPSocket failed");
    }

    if (m_context.verbose) LOGI(TAG, "Starting accumulator thread...");
    m_platform->CreateThread(&m_accumulatorThreadHandle, accumulatorThreadEntry, this);
    if (m_context.verbose) LOGI(TAG, "Controller initialized successfully");
}

Controller::~Controller() {
    // Release all held keys to prevent them from getting stuck on exit
    if (m_inputInjector && m_inputInjector->IsAvailable()) {
        std::lock_guard<std::mutex> lock(m_context.clientsMutex);
        for (auto& portPair : m_context.portStates) {
            int port = portPair.first;
            PortState& state = portPair.second;
            for (const auto& pair : state.overallKeyStates) {
                if (pair.second) {
                    int vk = pair.first;
                    if (IsMouseButtonCode(vk)) {
                        m_inputInjector->SendMouseButtonUp(vk - VK_MOUSE_LEFT_BUTTON);
                    } else if (!IsSpecialMouseCode(vk)) {
                        m_inputInjector->SendKeyUp(vk, port);
                    }
                }
            }
        }
    }
    if (m_accumulatorThreadHandle) {
        // This is tricky without a proper shutdown signal.
        // In a real app, you'd signal the thread to exit.
        m_platform->DetachThread(m_accumulatorThreadHandle);
    }
    if (m_sharedMemoryHandle) {
        m_platform->CloseSharedMemory(m_sharedMemoryHandle);
    }
    if (m_udpSocket != Platform::INVALID_SOCKET_HANDLE) {
        m_platform->CloseSocket(m_udpSocket);
    }
    m_platform->CloseEvent(m_context.stateUpdateEvent);
    m_platform->SocketsCleanup();
}

Platform::SharedMemoryHandle Controller::initializeSharedMemory(SharedInput** outSharedInput) {
    void* memory_ptr = nullptr;
    Platform::SharedMemoryHandle handle = m_platform->CreateSharedMemory("WinlatorInputSharedMemory", sizeof(SharedInput), &memory_ptr);
    if (!handle) {
        LOGE(TAG, "CreateFileMapping failed");
        return nullptr;
    }

    auto* s = static_cast<SharedInput*>(memory_ptr);
    *outSharedInput = s;

    init_shared_header(s, m_platform);
    memset(&s->kb, 0, sizeof(s->kb));
    memset(&s->mouse, 0, sizeof(s->mouse));
    memset(&s->xi, 0, sizeof(s->xi));

    return handle;
}

void Controller::Run() {
    char packetBuffer[1024];
    if (m_context.verbose) LOGI(TAG, "Controller loop started");
    while (!SignalHandler::isShutdownRequested()) {
        std::string clientKey;
        int clientPort;
        int bytesReceived = m_platform->RecvFrom(m_udpSocket, packetBuffer, sizeof(packetBuffer), 0, clientKey, clientPort);
        if (bytesReceived > 0) {
            if (m_context.verbose) LOGV(TAG, "Received " + std::to_string(bytesReceived) + " bytes from " + clientKey);
            // Decrypt packet if needed
            bool isValid;
            auto decryptedData = decryptPacket(packetBuffer, bytesReceived, isValid);
            if (isValid) {
                handlePacket(decryptedData, clientKey, clientPort);
            }
            else {
                if (m_context.verbose) {
                    LOGW(TAG, "Invalid or expired packet from " + clientKey);
                }
            }
        }
    }
    if (m_context.verbose) LOGI(TAG, "Controller loop exited");
}

void Controller::SetMinPressDuration(int ms) {
    m_context.minPressDurationMs = ms;
}

std::vector<uint8_t> Controller::decryptPacket(const char* buffer, int size, bool& outIsValid) {
    outIsValid = true;
    
    if (!m_packetEncryptor) {
        if (m_context.verbose) LOGD(TAG, "No encryptor, using raw packet");
        // Return the raw packet data as-is
        return std::vector<uint8_t>(buffer, buffer + size);
    }
    
    if (m_context.verbose) LOGD(TAG, "Decrypting packet...");
    std::vector<uint8_t> encryptedData(buffer, buffer + size);
    std::vector<uint8_t> decryptedData;
    try {
        decryptedData = m_packetEncryptor->DecryptPacket(encryptedData, outIsValid);
    } catch (...) {
        LOGE(TAG, "Exception during packet decryption");
        outIsValid = false;
        return std::vector<uint8_t>();
    }
    
    if (!outIsValid) {
        if (m_context.verbose) LOGD(TAG, "Packet decryption failed");
        return std::vector<uint8_t>();
    }
    
    if (m_context.verbose) LOGD(TAG, "Decryption successful, decrypted size=" + std::to_string(decryptedData.size()));
    return decryptedData;
}

void Controller::handlePacket(std::vector<uint8_t> &decryptedData, const std::string& clientKey, uint16_t clientPort) {
    // Use decrypted data (or raw data if no encryption)
    const char* payload = reinterpret_cast<const char*>(decryptedData.data());
    size_t payloadSize = decryptedData.size();
    if (m_context.verbose) LOGD(TAG, "Packet decrypted, size=" + std::to_string(payloadSize));
    // Safety check: ensure we have at least 1 byte for packet type
    if (payloadSize < 1) {
        if (m_context.verbose) {
            LOGW(TAG, "Packet too small from " + clientKey);
        }
        return;
    }

    uint8_t packetType = static_cast<uint8_t>(payload[0]);
    const char* packetPayload = payload + 1;
    const size_t packetPayloadSize = payloadSize - 1;

    if (m_context.verbose) LOGD(TAG, "Packet type=" + std::to_string((int)packetType) + ", payloadSize=" + std::to_string(packetPayloadSize));

    switch (packetType) {
        case PACKET_CONFIG:
            handleConfigPacket(m_context, m_platform, clientKey, packetPayload, packetPayloadSize);
            break;
        case PACKET_STATE:
            handleStatePacket(m_context, m_platform, clientKey, packetPayload, packetPayloadSize);
            break;
        case PACKET_MOUSE:
            handleMousePacket(m_context, m_inputInjector, clientKey, packetPayload, packetPayloadSize);
            break;
        case PACKET_XINPUT:
            handleXInputPacket(m_context, m_platform, packetPayload, packetPayloadSize);
            break;
        case PACKET_SCREEN_CONFIG:
            handleScreenConfigPacket(m_context, m_platform, packetPayload, packetPayloadSize);
            break;
        case PACKET_SCREEN_STOP:
            handleScreenStopPacket(m_context, m_platform);
            break;
        case PACKET_MOUSE_CONFIG:
            handleMouseConfigPacket(m_context, packetPayload, packetPayloadSize);
            break;
        case PACKET_TEXT:
            handleTextInputPacket(m_context, m_inputInjector, clientKey, packetPayload, packetPayloadSize);
            break;
        case PACKET_COMMAND:
            handleCommandPacket(m_context, m_platform, clientKey, packetPayload, packetPayloadSize);
            break;
        case PACKET_KEYCODE_DOWN:
            handleKeycodeDownPacket(m_context, m_inputInjector, clientKey, packetPayload, packetPayloadSize);
            break;
        case PACKET_KEYCODE_UP:
            handleKeycodeUpPacket(m_context, m_inputInjector, clientKey, packetPayload, packetPayloadSize);
            break;
        case PACKET_SET_MOUSE_MODE:
            if (packetPayloadSize >= 1) {
                std::lock_guard<std::mutex> lock(m_context.clientsMutex);
                m_context.mouseMode = static_cast<MouseMode>(packetPayload[0]);
                if (m_context.verbose) {
                    const char* modeStr = (m_context.mouseMode == MouseMode::RELATIVE_MODE) ? "relative" :
                                        (m_context.mouseMode == MouseMode::ABSOLUTE_MODE) ? "absolute" : "touchscreen";
                    LOGI(TAG, "Set Mouse Mode: " + std::string(modeStr));
                }
            }
            break;
        default:
            if (m_context.verbose) {
                LOGW(TAG, "Unknown packet type: " + std::to_string((int)packetType));
            }
            break;
    }
}

void* Controller::accumulatorThreadEntry(void* context) {
    static_cast<Controller*>(context)->accumulatorThreadFunction();
    return nullptr;
}

void Controller::accumulatorThreadFunction() {
    const int tick_ms = 1;
    m_platform->SetCurrentThreadHighPriority();
    m_platform->SetTimerResolution(tick_ms);
    if (m_context.verbose) {
        LOGI(TAG, "Started Accumulator Thread");
    }
    while (true) {
        m_platform->WaitForEvent(m_context.stateUpdateEvent, tick_ms);
        uint64_t now = m_platform->GetTickCountMs();
        
        std::unordered_map<int, std::unordered_map<int, double>> portMaxDuties;
        bool hasNoClients = true;

        {
            std::lock_guard<std::mutex> lock(m_context.clientsMutex);
            hasNoClients = m_context.clients.empty();
            for (auto& clientPair : m_context.clients) {
                ClientConfig& config = clientPair.second;
                if (config.port == -1) continue;

                // Timeout check: clear duty if client is inactive
                if (config.lastUpdate + 5000 < now) {
                    config.currentDuty.assign(config.buttonCodes.size(), 0.0);
                    config.lastUpdate = now;
                }

                auto& duties = portMaxDuties[config.port];
                for (size_t i = 0; i < config.buttonCodes.size(); ++i) {
                    int vk = config.buttonCodes[i];
                    double duty = config.currentDuty[i];
                    if (duties.find(vk) == duties.end() || abs(duty) > abs(duties[vk])) {
                        duties[vk] = duty;
                    }
                }
            }
        }

        if (hasNoClients) {
            m_platform->Sleep(1000);
            continue;
        }

        // Process each port that has active duty cycles
        for (auto& portPair : portMaxDuties) {
            int port = portPair.first;
            auto& duties = portPair.second;
            PortState& state = m_context.portStates[port];
            
            std::unordered_map<int, bool> newOverallKeyStates;
            std::unordered_map<int, double> mouseMovementValues;

            for (auto const& pair : duties) {
                int vk = pair.first;
                double duty = pair.second;

                if (IsSpecialMouseCode(vk)) {
                    mouseMovementValues[vk] = duty;
                    continue;
                }

                KeyState& keyState = state.perKeyState[vk];
                uint64_t elapsedMs = now - keyState.lastUpdate;
                keyState.lastUpdate = now;
                bool currentKeyState = keyState.isPressed;

                if (duty == 0 || duty == 1) {
                    currentKeyState = (duty == 1);
                    keyState.accumulator = 0.0;
                } else {
                    keyState.accumulator += duty * elapsedMs;
                    if (currentKeyState) {
                        keyState.accumulator -= elapsedMs;
                        uint64_t pressStartTime = keyState.pressStartTime;
                        uint64_t pressDurationMs = now - pressStartTime;
                        if (pressDurationMs >= static_cast<uint64_t>(m_context.minPressDurationMs) && keyState.accumulator < 0) {
                            currentKeyState = false;
                        }
                    } else {
                        if (keyState.accumulator >= m_context.minPressDurationMs) {
                            keyState.pressStartTime = now;
                            currentKeyState = true;
                        }
                    }
                }
                keyState.isPressed = currentKeyState;
                newOverallKeyStates[vk] = currentKeyState;
            }

            // Global/Explicit keys and shared memory updates (primarily for Port 0)
            if (port == 0) {
                {
                    std::lock_guard<std::mutex> explicitLock(m_context.explicitKeysMutex);
                    for (const auto& pair : m_context.explicitKeyStates) {
                        if (pair.second) {
                            newOverallKeyStates[pair.first] = true;
                        }
                    }
                }
                
                // Update shared memory for keyboard state
                memset(m_context.sharedInput->kb.downMask, 0, sizeof(m_context.sharedInput->kb.downMask));
                for (const auto& pair : newOverallKeyStates) {
                    if (pair.second) {
                        int vk = pair.first;
                        int idx = vk >> 5;
                        uint32_t bit = 1u << (vk & 31);
                        m_context.sharedInput->kb.downMask[idx] |= bit;
                    }
                }
                m_context.sharedInput->kb.lastUpdateMillis = now;
            }

            if (m_inputInjector->IsAvailable()) {
                ProcessMouseMovement(mouseMovementValues, port);
                ProcessOverallKeyChanges(state.overallKeyStates, newOverallKeyStates, port);
            }
        }
    }
}

void Controller::ProcessOverallKeyChanges(std::unordered_map<int, bool>& oldStates, const std::unordered_map<int, bool>& newStates, int port) {
    // Handle key releases in two passes: non-modifiers first, then modifiers.
    // This prevents unintended behavior when releasing key combinations.
    for (const auto& pair : oldStates) {
        int vk = pair.first;
        if (pair.second && (!newStates.count(vk) || !newStates.at(vk)) && !IsModifierKey(vk)) {
            if (IsMouseButtonCode(vk)) {
                m_inputInjector->SendMouseButtonUp(vk - VK_MOUSE_LEFT_BUTTON, port);
            } else if (!IsSpecialMouseCode(vk)) {
                m_inputInjector->SendKeyUp(vk, port);
            }
            if (m_context.verbose) LOGD(TAG, "SendInput[" + std::to_string(port) + "]: VK_" + std::to_string(vk) + " UP");
        }
    }
    for (const auto& pair : oldStates) {
        int vk = pair.first;
        if (pair.second && (!newStates.count(vk) || !newStates.at(vk)) && IsModifierKey(vk)) {
            if (!IsSpecialMouseCode(vk)) { // Mouse buttons are not modifiers
                m_inputInjector->SendKeyUp(vk, port);
            }
            if (m_context.verbose) LOGD(TAG, "SendInput[" + std::to_string(port) + "]: VK_" + std::to_string(vk) + " UP (Modifier)");
        }
    }

    // Handle key presses in two passes: modifiers first, then non-modifiers.
    // This ensures correct shortcut handling (e.g., Ctrl+C).
    for (const auto& pair : newStates) {
        int vk = pair.first;
        if (pair.second && (!oldStates.count(vk) || !oldStates[vk]) && IsModifierKey(vk)) {
            if (!IsSpecialMouseCode(vk)) {
                m_inputInjector->SendKeyDown(vk, port);
            }
            if (m_context.verbose) LOGD(TAG, "SendInput[" + std::to_string(port) + "]: VK_" + std::to_string(vk) + " DOWN (Modifier)");
        }
    }
    for (const auto& pair : newStates) {
        int vk = pair.first;
        if (pair.second && (!oldStates.count(vk) || !oldStates[vk]) && !IsModifierKey(vk)) {
            if (IsMouseButtonCode(vk)) {
                m_inputInjector->SendMouseButtonDown(vk - VK_MOUSE_LEFT_BUTTON, port);
            } else if (!IsSpecialMouseCode(vk)) {
                m_inputInjector->SendKeyDown(vk, port);
            }
            if (m_context.verbose) LOGD(TAG, "SendInput[" + std::to_string(port) + "]: VK_" + std::to_string(vk) + " DOWN");
        }
    }

    oldStates = newStates;
}

void Controller::ProcessMouseMovement(const std::unordered_map<int, double>& mouseMovementValues, int port) {
    PortState& state = m_context.portStates[port];
    double moveX = mouseMovementValues.count(VK_MOUSE_MOVE_X) ? mouseMovementValues.at(VK_MOUSE_MOVE_X) : 0;
    double moveY = mouseMovementValues.count(VK_MOUSE_MOVE_Y) ? mouseMovementValues.at(VK_MOUSE_MOVE_Y) : 0;
    double wheel = mouseMovementValues.count(VK_MOUSE_WHEEL) ? mouseMovementValues.at(VK_MOUSE_WHEEL) : 0;

    if (moveX == 0 && moveY == 0) {
        int cursorX, cursorY;
        m_platform->GetCursorPosition(cursorX, cursorY);
        state.mouseAnchorX = state.prevMouseX = cursorX;
        state.mouseAnchorY = state.prevMouseY = cursorY;
    } else {
        if (isTouchScreenMode(moveX, moveY)) {
            // Absolute normalized mode (Touchscreen)
            int32_t absX = static_cast<int32_t>(moveX * 65535);
            int32_t absY = static_cast<int32_t>(moveY * 65535);
            m_inputInjector->SendMouseMoveAbsolute(absX, absY, port);
        } else if (m_context.mouseMode == MouseMode::ABSOLUTE_MODE) {
            // Absolute cursor mode: accumulate deltas onto port's anchor
            state.mouseAnchorX += static_cast<int32_t>(moveX);
            state.mouseAnchorY += static_cast<int32_t>(moveY);
            m_inputInjector->SendMouseMoveAbsolute(state.mouseAnchorX, state.mouseAnchorY, port);
        } else {
            // Relative movement mode
            m_inputInjector->SendMouseMove(static_cast<int32_t>(moveX), static_cast<int32_t>(moveY), port);
        }
    }

    if (wheel != 0) {
        m_inputInjector->SendMouseWheel(static_cast<int>(wheel), port);
    }
}

uint16_t Controller::GetControllerPort() const { return m_controllerPort; }
uint16_t Controller::GetScreenPort() const { return m_context.screenPort; }

std::unique_ptr<IController> CreateController(Platform* platform, IInputInjector* inputInjector, bool verbose, ServerType serverType, PacketEncryptor* packetEncryptor, IImageEncoder* imageEncoder, IScreenCapture* screenCapture) {
    return std::make_unique<Controller>(platform, inputInjector, verbose, serverType, packetEncryptor, imageEncoder, screenCapture);
}