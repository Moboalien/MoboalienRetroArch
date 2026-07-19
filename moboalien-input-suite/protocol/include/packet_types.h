#pragma once
#include <cstdint>
#include <vector>

enum PacketType : uint8_t {
    PACKET_CONFIG = 0,
    PACKET_STATE = 1,
    PACKET_MOUSE = 2,
    PACKET_XINPUT = 3,
    PACKET_MOUSE_CONFIG = 4,
    PACKET_SCREEN_CONFIG = 5,
    PACKET_SCREEN_STOP = 6,
    PACKET_TEXT = 7,
    PACKET_COMMAND = 8,
    PACKET_KEYCODE_DOWN = 9,
    PACKET_KEYCODE_UP = 10,
    PACKET_HELLO = 11,
    PACKET_HELLO_RESPONSE = 12,
    PACKET_AUTH = 13,
    PACKET_AUTH_RESULT = 14,
    PACKET_SET_MOUSE_MODE = 15,
    PACKET_REQUEST_KEYFRAME = 16
};

enum MouseInputMode : uint8_t {
    MOUSE_MODE_RELATIVE = 0,
    MOUSE_MODE_ABSOLUTE = 1,
    MOUSE_MODE_TOUCHSCREEN = 2
};

struct MousePacket {
    float x, y;
    float wheel;
    uint32_t buttons;
};

struct MouseConfigPacket {
    uint8_t mode; // See MouseInputMode
};

struct XInputPacket {
    uint8_t controllerIndex;
    uint16_t buttons;
    float leftTrigger;
    float rightTrigger;
    float thumbLX, thumbLY;
    float thumbRX, thumbRY;
};

struct ScreenConfigPacket {
    // width and height are implicit from screen metrics
    uint8_t fps;
    uint32_t bitrate; // in kbps
    float scale;
    uint8_t quality;
    uint8_t method; // 0=GDI, 1=DD
    uint8_t streamingMode; // 0=MJPEG, 1=Differential
    uint8_t minQuality;
    uint32_t clientBuffer; // in KB
    bool cursorEnabled;
};

struct TextInputPacket {
    uint16_t length;
    // Variable length text follows immediately after this struct
};

struct CommandPacket {
    uint16_t length;
    // Variable length command follows immediately after this struct
};

struct KeycodePacket {
    uint16_t count;
    // Variable length array of uint32_t keycodes follows immediately after this struct
};

struct HelloPacket {
    uint32_t version;
    uint32_t serviceFlags;
};

struct HelloResponsePacket {
    uint32_t capabilities;
    uint32_t version; // Server protocol version
    uint16_t controllerPort;
    uint16_t screenPort;
    uint64_t serverId;
    uint16_t serverType;
    bool passwordRequired;
    uint8_t saltLength;
    // Salt bytes follow
};

// Current protocol version (increment when breaking wire-compatible changes)
static constexpr uint32_t PROTOCOL_VERSION = 1;

struct AuthPacket {
    uint16_t dataLength;
    // Encrypted data follows
};

struct SetMouseModePacket {
    uint8_t mode; // See MouseInputMode
};

struct RequestKeyframePacket {
    // No payload needed for a simple signal
};

struct AuthResultPacket {
    uint8_t result; // 0 = Fail, 1 = Success
    uint64_t serverTicks; // Current server timestamp in milliseconds
};