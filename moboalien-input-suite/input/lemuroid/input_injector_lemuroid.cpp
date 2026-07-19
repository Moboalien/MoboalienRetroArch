#include "i_input_injector.h"
#include "logger.h"
#include <cstdint>
#include <memory>

static const char* TAG = "InputInjectorLemuroid";

// ---------------------------------------------------------------------------
// Callback types set from JNI (MoboAlienServer.kt)
// ---------------------------------------------------------------------------

// sendKeyEvent(action, keyCode, port)
typedef void (*KeyEventCallback)(int action, int keyCode, int port);

static KeyEventCallback g_keyEventCallback = nullptr;

extern "C" void MoboAlien_SetKeyEventCallback(KeyEventCallback cb) {
    g_keyEventCallback = cb;
}

// ---------------------------------------------------------------------------
// IInputInjector implementation
// ---------------------------------------------------------------------------

class InputInjectorLemuroid final : public IInputInjector {
public:
    // Android key event action constants (mirror android.view.KeyEvent)
    static constexpr int ACTION_DOWN = 0;
    static constexpr int ACTION_UP   = 1;

    void SendKeyDown(int virtualKey, int port) override {
        LOGD(TAG, "SendKeyDown: VK_" + std::to_string(virtualKey));
        if (g_keyEventCallback)
            g_keyEventCallback(ACTION_DOWN, virtualKey, 0);
        else
            LOGW(TAG, "SendKeyDown: no callback set for VK " + std::to_string(virtualKey));
    }

    void SendKeyUp(int virtualKey, int port) override {
        LOGD(TAG, "SendKeyUp: VK_" + std::to_string(virtualKey));
        if (g_keyEventCallback)
            g_keyEventCallback(ACTION_UP, virtualKey, 0);
        else
            LOGW(TAG, "SendKeyUp: no callback set for VK " + std::to_string(virtualKey));
    }

    // Mouse / text / command -- not applicable for emulator input
    void SendMouseMove(int, int)           override {}
    void SendMouseMoveAbsolute(int, int)   override {}
    void SendMouseButtonDown(int)          override {}
    void SendMouseButtonUp(int)            override {}
    void SendMouseWheel(int)               override {}
    void SendTextInput(const char*, uint16_t, bool) override {}

    bool IsAvailable() override { return g_keyEventCallback != nullptr; }
    void SetMethod(InputMethod)            override {}
    void SetMouseMethod(MouseMethod)       override {}
};

std::unique_ptr<IInputInjector> CreateInputInjector() {
    return std::make_unique<InputInjectorLemuroid>();
}
