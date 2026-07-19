/*
Input Injector Implementation - Winlator Input Suite

Direct SendInput calls for immediate input injection with zero polling delay.
Provides perfect timing for sigma-delta duty cycle modulation.
*/

#include <windows.h>
#include <thread>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "input_injector.h"
#include "utils.h"

static const char* TAG = "InputInjector";

static const int TEXT_INPUT_DELAY_MS = 10;

static std::queue<std::string> textQueue;
static std::mutex queueMutex;
static std::condition_variable queueCondition;
static std::thread workerThread;
static bool workerRunning = false;
static InputInjector* workerInstance = nullptr;

static void TextInputWorker() {
    while (workerRunning) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCondition.wait(lock, [] { return !textQueue.empty() || !workerRunning; });
        
        if (!workerRunning) break;
        
        std::string text = textQueue.front();
        textQueue.pop();
        lock.unlock();
        
        workerInstance->SendTextInputSync(text);
    }
}

InputInjector::InputInjector() : m_inputMethod(InputMethod::SENDINPUT_SCANCODE), m_mouseMethod(MouseMethod::MOUSE_EVENT_METHOD), m_cacheInitialized(false) {
    m_isAvailable = [] {
        HDESK hDesk = GetThreadDesktop(GetCurrentThreadId());
        if (!hDesk) {
            LOGE(TAG, "No interactive desktop access. Input injection will be disabled");
            return false;
        }
        LOGI(TAG, "Interactive desktop access confirmed. Input injection enabled");
        return true;
    }();
    
    if (!workerRunning) {
        workerInstance = this;
        workerRunning = true;
        workerThread = std::thread(TextInputWorker);
    }
}

bool InputInjector::IsAvailable() {
    return m_isAvailable;
}

void InputInjector::SetMethod(InputMethod method) {
    m_inputMethod = method;
    const char* methodName = "Unknown";
    switch (method) {
        case InputMethod::SENDINPUT_SCANCODE: methodName = "SendInput+SCANCODE"; break;
        case InputMethod::KEYBD_EVENT: methodName = "keybd_event"; break;
        case InputMethod::POST_MESSAGE: methodName = "PostMessage"; break;
    }
    LOGI(TAG, "Input method: " + std::string(methodName));
}

void InputInjector::SetMouseMethod(MouseMethod method) {
    m_mouseMethod = method;
    const char* methodName = "Unknown";
    switch (method) {
        case MouseMethod::SENDINPUT: methodName = "SendInput"; break;
        case MouseMethod::MOUSE_EVENT_METHOD: methodName = "mouse_event"; break;
        case MouseMethod::SETCURSORPOS: methodName = "SetCursorPos"; break;
    }
    LOGI(TAG, "Mouse method: " + std::string(methodName));
}

void InputInjector::EnsureScanCodeCached() {
    if (m_cacheInitialized) return;
    for (int i = 0; i < 256; ++i) {
        m_scanCodeCache[i] = (WORD)MapVirtualKey(i, MAPVK_VK_TO_VSC);
    }
    m_cacheInitialized = true;
}

bool InputInjector::IsExtendedKey(int virtualKey) {
    return (virtualKey == VK_RMENU || virtualKey == VK_RCONTROL || 
            virtualKey == VK_LEFT || virtualKey == VK_RIGHT || 
            virtualKey == VK_UP || virtualKey == VK_DOWN || 
            virtualKey == VK_HOME || virtualKey == VK_DELETE || 
            virtualKey == VK_PRIOR || virtualKey == VK_NEXT || 
            virtualKey == VK_END || virtualKey == VK_INSERT || 
            virtualKey == VK_NUMLOCK || virtualKey == VK_SNAPSHOT || 
            virtualKey == VK_DIVIDE || virtualKey == VK_LWIN || 
            virtualKey == VK_RWIN || virtualKey == VK_APPS ||
            virtualKey == VK_VOLUME_UP || virtualKey == VK_VOLUME_DOWN ||
            virtualKey == VK_MEDIA_NEXT_TRACK || virtualKey == VK_MEDIA_PREV_TRACK ||
            virtualKey == VK_MEDIA_STOP || virtualKey == VK_VOLUME_MUTE
    );
}

void InputInjector::SendKeyDown(int virtualKey) {
    if (!IsAvailable()) return;
    
    if (m_inputMethod == InputMethod::POST_MESSAGE) {
        HWND hwnd = GetForegroundWindow();
        if (hwnd) {
            EnsureScanCodeCached();
            WORD scanCode = m_scanCodeCache[virtualKey & 0xFF];
            LPARAM lParam = (scanCode << 16) | 1;
            PostMessage(hwnd, WM_KEYDOWN, virtualKey, lParam);
        }
    } else if (m_inputMethod == InputMethod::KEYBD_EVENT) {
        EnsureScanCodeCached();
        WORD scanCode = m_scanCodeCache[virtualKey & 0xFF];
        keybd_event((BYTE)virtualKey, (BYTE)scanCode, 0, 0);
    } else {
        EnsureScanCodeCached();
        WORD scanCode = m_scanCodeCache[virtualKey & 0xFF];
        bool isExtended = IsExtendedKey(virtualKey);
        
        INPUT inp = {0};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = (WORD)virtualKey;
        inp.ki.wScan = scanCode;
        inp.ki.dwFlags = KEYEVENTF_SCANCODE | (isExtended ? KEYEVENTF_EXTENDEDKEY : 0);
        SendInput(1, &inp, sizeof(INPUT));
    }
}

void InputInjector::SendKeyUp(int virtualKey) {
    if (!IsAvailable()) return;
    
    if (m_inputMethod == InputMethod::POST_MESSAGE) {
        HWND hwnd = GetForegroundWindow();
        if (hwnd) {
            EnsureScanCodeCached();
            WORD scanCode = m_scanCodeCache[virtualKey & 0xFF];
            LPARAM lParam = (scanCode << 16) | 0xC0000001; // Key up flags
            PostMessage(hwnd, WM_KEYUP, virtualKey, lParam);
        }
    } else if (m_inputMethod == InputMethod::KEYBD_EVENT) {
        EnsureScanCodeCached();
        WORD scanCode = m_scanCodeCache[virtualKey & 0xFF];
        keybd_event((BYTE)virtualKey, (BYTE)scanCode, KEYEVENTF_KEYUP, 0);
    } else {
        EnsureScanCodeCached();
        WORD scanCode = m_scanCodeCache[virtualKey & 0xFF];
        bool isExtended = IsExtendedKey(virtualKey);
        
        INPUT inp = {0};
        inp.type = INPUT_KEYBOARD;
        inp.ki.wVk = (WORD)virtualKey;
        inp.ki.wScan = scanCode;
        inp.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP | (isExtended ? KEYEVENTF_EXTENDEDKEY : 0);
        SendInput(1, &inp, sizeof(INPUT));
    }
}

void InputInjector::SendMouseMove(int deltaX, int deltaY) {
    if (!IsAvailable() || (deltaX == 0 && deltaY == 0)) return;
    switch (m_mouseMethod) {
        case MouseMethod::MOUSE_EVENT_METHOD:
            mouse_event(MOUSEEVENTF_MOVE, deltaX, deltaY, 0, 0);
            break;
        case MouseMethod::SETCURSORPOS: {
            POINT pt;
            if (GetCursorPos(&pt)) {
                SetCursorPos(pt.x + deltaX, pt.y + deltaY);
            }
            break;
        }
        default: {
            INPUT inp = {0};
            inp.type = INPUT_MOUSE;
            inp.mi.dwFlags = MOUSEEVENTF_MOVE;
            inp.mi.dx = deltaX;
            inp.mi.dy = deltaY;
            SendInput(1, &inp, sizeof(INPUT));
        }
    }
}

void InputInjector::SendMouseMoveAbsolute(int x, int y) {
    if (!IsAvailable()) return;
    
    INPUT inp = {0};
    inp.type = INPUT_MOUSE;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    
    // Convert to 0-65535 coordinate system
    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    inp.mi.dx = (LONG)((x * 65535) / sx);
    inp.mi.dy = (LONG)((y * 65535) / sy);
    
    SendInput(1, &inp, sizeof(INPUT));
}

void InputInjector::SendMouseButtonDown(int button) {
    if (!IsAvailable()) return;
    
    INPUT inp = {0};
    inp.type = INPUT_MOUSE;
    
    switch (button) {
        case 0: inp.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; break;
        case 1: inp.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; break;
        case 2: inp.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN; break;
        case 3: inp.mi.dwFlags = MOUSEEVENTF_XDOWN; inp.mi.mouseData = XBUTTON1; break;
        case 4: inp.mi.dwFlags = MOUSEEVENTF_XDOWN; inp.mi.mouseData = XBUTTON2; break;
        default: return;
    }
    
    SendInput(1, &inp, sizeof(INPUT));
}

void InputInjector::SendMouseButtonUp(int button) {
    if (!IsAvailable()) return;
    
    INPUT inp = {0};
    inp.type = INPUT_MOUSE;
    
    switch (button) {
        case 0: inp.mi.dwFlags = MOUSEEVENTF_LEFTUP; break;
        case 1: inp.mi.dwFlags = MOUSEEVENTF_RIGHTUP; break;
        case 2: inp.mi.dwFlags = MOUSEEVENTF_MIDDLEUP; break;
        case 3: inp.mi.dwFlags = MOUSEEVENTF_XUP; inp.mi.mouseData = XBUTTON1; break;
        case 4: inp.mi.dwFlags = MOUSEEVENTF_XUP; inp.mi.mouseData = XBUTTON2; break;
        default: return;
    }
    
    SendInput(1, &inp, sizeof(INPUT));
}

void InputInjector::SendMouseWheel(int wheelDelta) {
    if (!IsAvailable() || wheelDelta == 0) return;
    
    INPUT inp = {0};
    inp.type = INPUT_MOUSE;
    inp.mi.dwFlags = MOUSEEVENTF_WHEEL;
    inp.mi.mouseData = wheelDelta;
    
    SendInput(1, &inp, sizeof(INPUT));
}

void InputInjector::SendTextInputSync(const std::string& text) {
    for (char c : text) {
        SHORT vk = VkKeyScan(c);
        if (vk == -1) continue;
        
        int virtualKey = vk & 0xFF;
        bool needShift = (vk & 0x100) != 0;
        
        if (needShift) SendKeyDown(VK_SHIFT);
        SendKeyDown(virtualKey);
        Sleep(TEXT_INPUT_DELAY_MS);
        SendKeyUp(virtualKey);
        if (needShift) SendKeyUp(VK_SHIFT);
        Sleep(TEXT_INPUT_DELAY_MS);
    }
}

void InputInjector::SendTextInput(const char* text, uint16_t length, bool async) {
    if (!IsAvailable() || length == 0) return;
    
    if (async) {
        std::lock_guard<std::mutex> lock(queueMutex);
        textQueue.emplace(text, length);
        queueCondition.notify_one();
    } else {
        SendTextInputSync(std::string(text, length));
    }
}

std::unique_ptr<IInputInjector> CreateInputInjector() {
    return std::make_unique<InputInjector>();
}