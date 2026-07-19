/*
Helper executable that polls the shared memory region and synthesizes keyboard/mouse
events into the system by calling SendInput(). This is useful when an application
only observes synthesized (SendInput) events or when running inside environments
that need this bridging.

The program polls at a fixed interval and applies changes found in shared memory.
*/

#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include "shared_input.h"

int main(int argc, char* argv[]) {
    bool absoluteMode = false;
    if (argc > 1 && strcmp(argv[1], "--absolute") == 0) {
        absoluteMode = true;
        std::cout << "Using absolute mouse mode" << std::endl;
    } else {
        std::cout << "Using relative mouse mode (default)" << std::endl;
    }
    // Open shared memory
    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, "WinlatorInputSharedMemory");
    if (!hMapFile) {
        std::cerr << "OpenFileMapping failed. Is the server running?" << std::endl;
        return 1;
    }
    void* pBuf = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0);
    if (!pBuf) {
        std::cerr << "MapViewOfFile failed" << std::endl;
        return 1;
    }
    std::cout << "sendinput_helper running. Polling shared memory for input state..." << std::endl;

    SharedInput* s = reinterpret_cast<SharedInput*>(pBuf);

    // Track previous states
    uint32_t prevMask[8] = {0};
    uint32_t prevMouseButtons = 0;
    int32_t prevMouseX = 0, prevMouseY = 0;

    while (true) {
        // Copy atomically from shared memory (simplified; production should use synchronization)
        KeyboardState kb = s->kb;
        MouseState mouse = s->mouse;

        // Keyboard: compute changed bits
        for (int i = 0; i < 256; ++i) {
            uint32_t bit = 1u << (i & 31);
            uint32_t idx = i >> 5;
            bool was = (prevMask[idx] & bit) != 0;
            bool now = (kb.downMask[idx] & bit) != 0;
            if (was != now) {
                // synthesize key event
                INPUT inp = {0};
                inp.type = INPUT_KEYBOARD;
                inp.ki.wVk = (WORD)i;
                inp.ki.dwFlags = now ? 0 : KEYEVENTF_KEYUP;
                SendInput(1, &inp, sizeof(inp));
                std::cout << "SendInput: VK_" << i << (now ? " DOWN" : " UP") << std::endl;
            }
        }

        // Save current mask
        memcpy(prevMask, kb.downMask, sizeof(prevMask));

        // Mouse: configurable movement mode
        INPUT m[2];
        ZeroMemory(m, sizeof(m));
        m[0].type = INPUT_MOUSE;
        
        if (absoluteMode) {
            // Absolute mode
            m[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
            int sx = GetSystemMetrics(SM_CXSCREEN);
            int sy = GetSystemMetrics(SM_CYSCREEN);
            m[0].mi.dx = (LONG)((mouse.x * 65535) / std::max(1, sx));
            m[0].mi.dy = (LONG)((mouse.y * 65535) / std::max(1, sy));
        } else {
            // Relative mode (default)
            m[0].mi.dwFlags = MOUSEEVENTF_MOVE;
            m[0].mi.dx = mouse.x - prevMouseX;
            m[0].mi.dy = mouse.y - prevMouseY;
        }
        prevMouseX = mouse.x;
        prevMouseY = mouse.y;

        // Wheel example
        m[1].type = INPUT_MOUSE;
        m[1].mi.dwFlags = MOUSEEVENTF_WHEEL;
        m[1].mi.mouseData = mouse.wheel;

        // Mouse buttons
        uint32_t mouseChanged = prevMouseButtons ^ mouse.buttons;
        if (mouseChanged) {
            for (int i = 0; i < 5; ++i) {
                if (mouseChanged & (1u << i)) {
                    INPUT mb = {0};
                    mb.type = INPUT_MOUSE;
                    bool pressed = (mouse.buttons & (1u << i)) != 0;
                    switch (i) {
                        case 0: mb.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
                        case 1: mb.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
                        case 2: mb.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
                        case 3: mb.mi.dwFlags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; mb.mi.mouseData = XBUTTON1; break;
                        case 4: mb.mi.dwFlags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; mb.mi.mouseData = XBUTTON2; break;
                    }
                    SendInput(1, &mb, sizeof(INPUT));
                    std::cout << "SendInput: Mouse button " << i << (pressed ? " DOWN" : " UP") << std::endl;
                }
            }
            prevMouseButtons = mouse.buttons;
        }

        // Send mouse move/wheel
        SendInput(2, m, sizeof(INPUT));
        static int lastWheel = 0;
        if (m[0].mi.dx != 0 || m[0].mi.dy != 0) {
            if (absoluteMode) {
                std::cout << "SendInput: Mouse move absolute (" << mouse.x << "," << mouse.y << ")" << std::endl;
            } else {
                std::cout << "SendInput: Mouse move delta (" << m[0].mi.dx << "," << m[0].mi.dy << ")" << std::endl;
            }
        }
        if (mouse.wheel != lastWheel) {
            std::cout << "SendInput: Mouse wheel " << mouse.wheel << std::endl;
            lastWheel = mouse.wheel;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    UnmapViewOfFile(pBuf);
    CloseHandle(hMapFile);
    return 0;
}
