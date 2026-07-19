/*
Input Injector Implementation - Winlator Input Suite

Provides a concrete implementation of the IInputInjector interface using
the Windows SendInput API.
*/

#pragma once

#include "i_input_injector.h"
#include <windows.h>

class InputInjector final : public IInputInjector {
public:
    InputInjector();

    void SendKeyDown(int virtualKey, int port = 0) override;
    void SendKeyUp(int virtualKey, int port = 0) override;
    void SendMouseMove(int deltaX, int deltaY) override;
    void SendMouseMoveAbsolute(int x, int y) override;
    void SendMouseButtonDown(int button) override;
    void SendMouseButtonUp(int button) override;
    void SendMouseWheel(int wheelDelta) override;
    void SendTextInput(const char* text, uint16_t length, bool async = true) override;
    void SendTextInputSync(const std::string& text);
    bool IsAvailable() override;
    void SetMethod(InputMethod method) override;
    void SetMouseMethod(MouseMethod method) override;

private:
    void EnsureScanCodeCached();
    bool IsExtendedKey(int virtualKey);
    InputMethod m_inputMethod;
    MouseMethod m_mouseMethod;
    WORD m_scanCodeCache[256] = {0};
    bool m_cacheInitialized = false;
    bool m_isAvailable;
};