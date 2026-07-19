#pragma once

#include <cstdint>
#include <memory>

// Input method enumeration
enum class InputMethod {
    SENDINPUT_SCANCODE,  // SendInput with scan codes (default, DirectInput compatible)
    KEYBD_EVENT,         // Legacy keybd_event API
    POST_MESSAGE         // PostMessage to foreground window
};

enum class MouseMethod {
    SENDINPUT,          // SendInput API (default)
    MOUSE_EVENT_METHOD,        // Legacy mouse_event API
    SETCURSORPOS        // Direct cursor positioning
};

class IInputInjector {
public:
    virtual ~IInputInjector() = default;

    // Keyboard input functions
    virtual void SendKeyDown(int virtualKey, int port = 0) = 0;
    virtual void SendKeyUp(int virtualKey, int port = 0) = 0;

    // Mouse input functions
    virtual void SendMouseMove(int deltaX, int deltaY, int port = 0) = 0;
    virtual void SendMouseMoveAbsolute(int x, int y, int port = 0) = 0;
    virtual void SendMouseButtonDown(int button, int port = 0) = 0;  // 0=left, 1=right, 2=middle, 3=x1, 4=x2
    virtual void SendMouseButtonUp(int button, int port = 0) = 0;
    virtual void SendMouseWheel(int wheelDelta, int port = 0) = 0;

    // Text input functions
    virtual void SendTextInput(const char* text, uint16_t length, int port = 0, bool async = true) = 0;

    // Configuration
    virtual bool IsAvailable() = 0;
    virtual void SetMethod(InputMethod method) = 0;
    virtual void SetMouseMethod(MouseMethod method) = 0;
};

std::unique_ptr<IInputInjector> CreateInputInjector();