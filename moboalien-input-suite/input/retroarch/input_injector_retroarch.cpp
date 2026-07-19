#include "i_input_injector.h"
#include "logger.h"
#include <cstdint>
#include <memory>
#include <string>

static const char* TAG = "InputInjectorRetroArch";

extern "C" void moboalien_inject_key(int port, int keycode, int down);
extern "C" void moboalien_inject_hotkey(int retrok, int down);
extern "C" void moboalien_inject_mouse_move(int port, int x, int y, int is_absolute);
extern "C" void moboalien_inject_mouse_button(int port, int button, int down);
extern "C" void moboalien_inject_mouse_wheel(int port, int delta);

static bool is_retrok_hotkey(int key)
{
    return key >= 256 || key == 13 || key == 8 || key == 27 || key == 32 || key == 9;
}

class InputInjectorRetroArch final : public IInputInjector {
public:
    void SendKeyDown(int virtualKey, int port) override {
        LOGD(TAG, "SendKeyDown: " + std::to_string(virtualKey) + " port: " + std::to_string(port));
        if (is_retrok_hotkey(virtualKey))
            moboalien_inject_hotkey(virtualKey, 1);
        else
            moboalien_inject_key(port, virtualKey, 1);
    }

    void SendKeyUp(int virtualKey, int port) override {
        LOGD(TAG, "SendKeyUp: " + std::to_string(virtualKey) + " port: " + std::to_string(port));
        if (is_retrok_hotkey(virtualKey))
            moboalien_inject_hotkey(virtualKey, 0);
        else
            moboalien_inject_key(port, virtualKey, 0);
    }

    void SendMouseMove(int deltaX, int deltaY, int port) override {
        moboalien_inject_mouse_move(port, deltaX, deltaY, 0);
    }

    void SendMouseMoveAbsolute(int x, int y, int port) override {
        moboalien_inject_mouse_move(port, x, y, 1);
    }

    void SendMouseButtonDown(int button, int port) override {
        moboalien_inject_mouse_button(port, button, 1);
    }

    void SendMouseButtonUp(int button, int port) override {
        moboalien_inject_mouse_button(port, button, 0);
    }

    void SendMouseWheel(int wheelDelta, int port) override {
        moboalien_inject_mouse_wheel(port, wheelDelta);
    }

    void SendTextInput(const char* text, uint16_t length, int port, bool async) override {
        // Not implemented for RetroArch yet
    }

    bool IsAvailable()                                  override { return true; }
    void SetMethod(InputMethod)                         override {}
    void SetMouseMethod(MouseMethod)                    override {}
};

std::unique_ptr<IInputInjector> CreateInputInjector() {
    return std::make_unique<InputInjectorRetroArch>();
}
