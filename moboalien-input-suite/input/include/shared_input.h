#pragma once
#include <cstdint>
#include "platform.h"
#include <chrono>
//
// Shared memory layout for keyboard, mouse and XInput states.
//
// This header is included by all components. Keep it POD and versioned so
// different components can detect layout mismatches.
//

#define SHARED_INPUT_MAGIC 0x57494E50 // 'WINP'
#define SHARED_INPUT_VERSION 1

#pragma pack(push, 1)
struct KeyboardState {
    uint32_t downMask[8]; // 256 bits - 32 bytes per entry * 8 = 256 bytes
    // Simple timestamp
    uint64_t lastUpdateMillis;
};

struct MouseState {
    int32_t x;
    int32_t y;
    int32_t wheel;
    uint32_t buttons; // bitmask for up to 32 buttons
    uint64_t lastUpdateMillis;
};

struct XInputGamepad {
    uint16_t buttons;
    uint8_t leftTrigger;
    uint8_t rightTrigger;
    int16_t sThumbLX;
    int16_t sThumbLY;
    int16_t sThumbRX;
    int16_t sThumbRY;
};

struct XInputState {
    uint32_t connectedMask; // bit per controller (0..3)
    XInputGamepad controllers[4];
    uint64_t lastUpdateMillis;
};

struct SharedInputHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t size; // total size of the mapped region
    uint64_t createdAt;
};

struct SharedInputState {
    KeyboardState kb;
    MouseState mouse;
    XInputState xi;
};

struct SharedInput {
    SharedInputHeader header;
    KeyboardState kb;
    MouseState mouse;
    XInputState xi;
};
#pragma pack(pop)

#define SHARED_MEM_NAME "WinlatorInputSharedMemory"

// Helpers
inline void init_shared_header(SharedInput* s, Platform* platform) { // NOLINT: Allow non-const pointer for output
    if (!s) return;
    s->header.magic = SHARED_INPUT_MAGIC;
    s->header.version = SHARED_INPUT_VERSION;
    s->header.size = sizeof(SharedInput);
    s->header.createdAt = platform->GetTickCountMs(); // Use injected platform object
}
