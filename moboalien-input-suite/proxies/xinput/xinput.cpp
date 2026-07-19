/*
XInput Proxy DLL - Moboalien Input Suite

This proxy DLL intercepts XInput API calls and can inject virtual controller
states from shared memory. It acts as a transparent layer between games and
the real XInput system.

How it works:
1. Game calls XInputGetState() → intercepted by this proxy
2. Proxy checks shared memory for injected controller data
3. If injection data exists → return virtual controller state
4. Otherwise → forward call to real XInput system

This enables remote controller input injection for games.
*/

#include <windows.h>
#include <xinput.h>
#include "shared_input.h"

// Handle to the real XInput DLL (xinput1_4.dll or xinput1_3.dll)
static HMODULE real_xinput = NULL;

// Cached shared memory for performance
static HANDLE cached_hMap = NULL;
static SharedInput* cached_pSharedMem = nullptr;

// Function pointer types for XInput API functions
typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI *XInputSetState_t)(DWORD, XINPUT_VIBRATION*);
typedef DWORD (WINAPI *XInputGetCapabilities_t)(DWORD, DWORD, XINPUT_CAPABILITIES*);

// Function pointers to the real XInput functions
static XInputGetState_t real_XInputGetState = nullptr;
static XInputSetState_t real_XInputSetState = nullptr;
static XInputGetCapabilities_t real_XInputGetCapabilities = nullptr;

/*
XInputGetState - Main interception point for controller input

This function is called by games to get controller state. We intercept it to:
1. Check if we have injected controller data in shared memory
2. If yes, return the injected state (for remote input)
3. If no, forward to real XInput (for local controllers)

Parameters:
- dwUserIndex: Controller index (0-3)
- pState: Pointer to receive controller state

Returns: ERROR_SUCCESS if controller connected, ERROR_DEVICE_NOT_CONNECTED if not
*/
extern "C" DWORD WINAPI XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) noexcept {
    // Lazy load the real XInput DLL on first call
    if (!real_xinput) {
        // Try xinput1_4.dll first (Windows 8+), then xinput1_3.dll (Windows 7)
        real_xinput = LoadLibraryA("xinput1_4.dll");
        if (!real_xinput) real_xinput = LoadLibraryA("xinput1_3.dll");
        if (!real_xinput) return ERROR_DEVICE_NOT_CONNECTED;
        
        // Get the real XInputGetState function
        real_XInputGetState = (XInputGetState_t)GetProcAddress(real_xinput, "XInputGetState");
        if (!real_XInputGetState) return ERROR_DEVICE_NOT_CONNECTED;
    }

    // Validate parameters
    if (!pState || dwUserIndex >= 4) {
        return ERROR_BAD_ARGUMENTS;
    }

    // Initialize cached shared memory on first access
    if (!cached_hMap) {
        cached_hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, "MoboalienInputSharedMemory");
        if (cached_hMap) {
            cached_pSharedMem = (SharedInput*)MapViewOfFile(cached_hMap, FILE_MAP_READ, 0, 0, 0);
        }
    }
    
    // Fast path: use cached shared memory
    if (cached_pSharedMem) {
        // Check if this controller index has injected data
        if (cached_pSharedMem->xi.connectedMask & (1u << dwUserIndex)) {
            // Return injected controller state from shared memory
            pState->dwPacketNumber++; // Increment packet number to indicate state change
            const XInputGamepad& pad = cached_pSharedMem->xi.controllers[dwUserIndex];
            pState->Gamepad.wButtons = pad.buttons;
            pState->Gamepad.bLeftTrigger = pad.leftTrigger;
            pState->Gamepad.bRightTrigger = pad.rightTrigger;
            pState->Gamepad.sThumbLX = pad.sThumbLX;
            pState->Gamepad.sThumbLY = pad.sThumbLY;
            pState->Gamepad.sThumbRX = pad.sThumbRX;
            pState->Gamepad.sThumbRY = pad.sThumbRY;
            
            return ERROR_SUCCESS;
        }
    }

    // No injected data found, forward to real XInput for physical controllers
    return real_XInputGetState(dwUserIndex, pState);
}

/*
XInputSetState - Controller vibration/rumble control

Forwards vibration commands to real XInput. We don't intercept this since
virtual controllers don't need vibration feedback.
*/
extern "C" DWORD WINAPI XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration) noexcept {
    // Lazy load real XInputSetState if needed
    if (!real_XInputSetState && real_xinput) {
        real_XInputSetState = (XInputSetState_t)GetProcAddress(real_xinput, "XInputSetState");
    }
    
    if (!real_XInputSetState) return ERROR_DEVICE_NOT_CONNECTED;
    return real_XInputSetState(dwUserIndex, pVibration);
}

/*
XInputGetCapabilities - Query controller capabilities

Forwards capability queries to real XInput. Games use this to determine
what features a controller supports (buttons, triggers, vibration, etc.)
*/
extern "C" DWORD WINAPI XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities) noexcept {
    // Lazy load real XInputGetCapabilities if needed
    if (!real_XInputGetCapabilities && real_xinput) {
        real_XInputGetCapabilities = (XInputGetCapabilities_t)GetProcAddress(real_xinput, "XInputGetCapabilities");
    }
    
    if (!real_XInputGetCapabilities) return ERROR_DEVICE_NOT_CONNECTED;
    return real_XInputGetCapabilities(dwUserIndex, dwFlags, pCapabilities);
}

/*
XInputEnable - Enable/disable XInput

Some games call this to enable/disable controller input. We provide a stub
since we always want input enabled for injection to work.
*/
extern "C" void WINAPI XInputEnable(BOOL enable) noexcept {
    // Do nothing - we always want XInput enabled for injection
}

/*
DLL Entry Point

Called when the DLL is loaded/unloaded. We don't need special initialization
so just return TRUE to indicate successful loading.
*/
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            // DLL loaded - no special initialization needed
            break;
        case DLL_PROCESS_DETACH:
            // DLL unloaded - cleanup if needed
            if (cached_pSharedMem) {
                UnmapViewOfFile(cached_pSharedMem);
                cached_pSharedMem = nullptr;
            }
            if (cached_hMap) {
                CloseHandle(cached_hMap);
                cached_hMap = NULL;
            }
            if (real_xinput) {
                FreeLibrary(real_xinput);
                real_xinput = NULL;
            }
            break;
    }
    return TRUE;
}