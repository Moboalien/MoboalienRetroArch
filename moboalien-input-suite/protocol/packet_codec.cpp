#include "packet_codec.h"
#include "utils.h"
#include "packet_types.h"

namespace Controller {

//=============================================================================
// Packet Encoding Functions
//=============================================================================

std::vector<char> CreateConfigPacket(const std::vector<int>& buttonCodes) {
    std::vector<char> packet;
    packet.push_back(PACKET_CONFIG);
    AppendInt32(packet, static_cast<int32_t>(buttonCodes.size()));
    for (int code : buttonCodes) {
        AppendInt32(packet, code);
    }
    return packet;
}

std::vector<char> CreateStatePacket(const std::vector<float>& states) {
    std::vector<char> packet;
    packet.push_back(PACKET_STATE);
    for (float state : states) {
        AppendFloat(packet, state);
    }
    return packet;
}

std::vector<char> CreateMousePacket(float x, float y, float wheel, uint32_t buttons) {
    std::vector<char> packet;
    packet.push_back(PACKET_MOUSE);
    AppendFloat(packet, x);
    AppendFloat(packet, y);
    AppendFloat(packet, wheel);
    // Integer buttons are appended in big-endian
    AppendInt32(packet, buttons);
    return packet;
}

std::vector<char> CreateMouseConfigPacket(uint8_t mode) {
    std::vector<char> packet;
    packet.push_back(PACKET_MOUSE_CONFIG);
    packet.push_back(mode);
    return packet;
}

std::vector<char> CreateXInputPacket(int controllerIndex, uint16_t buttons, float lt, float rt, float lx, float ly, float rx, float ry) {
    std::vector<char> packet;
    packet.push_back(PACKET_XINPUT);
    packet.push_back(static_cast<char>(controllerIndex)); // uint8_t, no endianness issue
    // Manually append uint16_t in big-endian format
    packet.push_back((buttons >> 8) & 0xFF); // MSB
    packet.push_back(buttons & 0xFF);        // LSB
    // Floats are now appended in big-endian
    AppendFloat(packet, lt);
    AppendFloat(packet, rt);
    AppendFloat(packet, lx);
    AppendFloat(packet, ly);
    AppendFloat(packet, rx);
    AppendFloat(packet, ry);
    return packet;
}

std::vector<char> CreateScreenConfigPacket(const ScreenConfigPacket& config) {
    std::vector<char> packet;
    packet.push_back(PACKET_SCREEN_CONFIG);
    packet.push_back(config.fps);
    AppendInt32(packet, config.bitrate);
    AppendFloat(packet, config.scale);
    packet.push_back(config.quality);
    packet.push_back(config.method);
    packet.push_back(config.streamingMode);
    packet.push_back(config.minQuality);
    AppendInt32(packet, config.clientBuffer);
    packet.push_back(config.cursorEnabled ? 1 : 0);
    return packet;
}

std::vector<char> CreateTextInputPacket(const char* text, uint16_t length) {
    std::vector<char> packet;
    packet.push_back(PACKET_TEXT);
    AppendInt16(packet, length);
    packet.insert(packet.end(), text, text + length);
    return packet;
}

std::vector<char> CreateCommandPacket(const char* command, uint16_t length) {
    std::vector<char> packet;
    packet.push_back(PACKET_COMMAND);
    AppendInt16(packet, length);
    packet.insert(packet.end(), command, command + length);
    return packet;
}

std::vector<char> CreateKeycodeDownPacket(const uint32_t* keycodes, uint16_t count) {
    std::vector<char> packet;
    packet.push_back(PACKET_KEYCODE_DOWN);
    AppendInt16(packet, count);
    for (uint16_t i = 0; i < count; ++i) {
        AppendInt32(packet, keycodes[i]);
    }
    return packet;
}

std::vector<char> CreateKeycodeUpPacket(const uint32_t* keycodes, uint16_t count) {
    std::vector<char> packet;
    packet.push_back(PACKET_KEYCODE_UP);
    AppendInt16(packet, count);
    for (uint16_t i = 0; i < count; ++i) {
        AppendInt32(packet, keycodes[i]);
    }
    return packet;
}

std::vector<char> CreateHelloPacket(uint32_t version, uint32_t serviceFlags) {
    std::vector<char> packet;
    packet.push_back(PACKET_HELLO);
    AppendInt32(packet, version);
    AppendInt32(packet, serviceFlags);
    return packet;
}

std::vector<char> CreateHelloResponsePacket(uint32_t capabilities, uint32_t version, uint16_t controllerPort, uint16_t screenPort, uint64_t serverId, uint16_t serverType, bool passwordRequired, const std::vector<uint8_t>& salt) {
    std::vector<char> packet;
    packet.push_back(PACKET_HELLO_RESPONSE);
    AppendInt32(packet, capabilities);
    AppendInt32(packet, version);
    AppendInt16(packet, controllerPort);
    AppendInt16(packet, screenPort);
    AppendInt64(packet, serverId);
    AppendInt16(packet, serverType);
    packet.push_back(passwordRequired ? 1 : 0);
    packet.push_back(static_cast<char>(salt.size()));
    packet.insert(packet.end(), salt.begin(), salt.end());
    return packet;
}

std::vector<char> CreateAuthPacket(const std::vector<uint8_t>& encryptedData) {
    std::vector<char> packet;
    packet.push_back(PACKET_AUTH);
    AppendInt16(packet, static_cast<uint16_t>(encryptedData.size()));
    packet.insert(packet.end(), encryptedData.begin(), encryptedData.end());
    return packet;
}

std::vector<char> CreateAuthResultPacket(bool success, int64_t serverTicks) {
    std::vector<char> packet;
    packet.push_back(PACKET_AUTH_RESULT);
    packet.push_back(success ? 1 : 0);
    AppendInt64(packet, int64_t(serverTicks));
    return packet;
}

std::vector<char> CreateRequestKeyframePacket() {
    std::vector<char> packet;
    packet.push_back(PACKET_REQUEST_KEYFRAME);
    return packet;
}


//=============================================================================
// Packet Deserialization Functions
//=============================================================================

bool ParseConfigPacket(const char* payload, size_t payloadSize, std::vector<int>& outButtonCodes) {
    if (payloadSize < sizeof(int32_t)) return false;
    const char* buffer = payload;
    int32_t numButtons = ReadInt32(buffer);
    buffer += sizeof(int32_t);

    if (numButtons < 0 || payloadSize < sizeof(int32_t) + static_cast<size_t>(numButtons) * sizeof(int32_t)) return false;

    outButtonCodes.assign(numButtons, 0);
    for (int i = 0; i < numButtons; ++i) {
        outButtonCodes[i] = ReadInt32(buffer);
        buffer += sizeof(int32_t);
    }
    return true;
}

bool ParseStatePacket(const char* payload, size_t payloadSize, size_t expectedCount, std::vector<float>& outStates) {
    size_t numFloats = payloadSize / sizeof(float);
    if (numFloats != expectedCount) return false;
    
    outStates.assign(numFloats, 0.0f);
    const char* buffer = payload;
    for (size_t i = 0; i < numFloats; ++i) {
        outStates[i] = ReadFloat(buffer);
        buffer += sizeof(float);
    }
    return true;
}

bool ParseMousePacket(const char* payload, size_t payloadSize, MousePacket& outPacket) {
    if (payloadSize < sizeof(MousePacket)) return false;
    const char* buffer = payload;
    outPacket.x = ReadFloat(buffer); buffer += sizeof(float);
    outPacket.y = ReadFloat(buffer); buffer += sizeof(float);
    outPacket.wheel = ReadFloat(buffer); buffer += sizeof(float);
    outPacket.buttons = ReadInt32(buffer);
    return true;
}

bool ParseMouseConfigPacket(const char* payload, size_t payloadSize, MouseConfigPacket& outPacket) {
    if (payloadSize < sizeof(uint8_t)) return false;
    outPacket.mode = static_cast<uint8_t>(*payload);
    return true;
}

bool ParseXInputPacket(const char* payload, size_t payloadSize, XInputPacket& outPacket) {
    if (payloadSize < sizeof(XInputPacket)) return false;
    const char* buffer = payload;
    outPacket.controllerIndex = static_cast<uint8_t>(*buffer); buffer += sizeof(uint8_t);
    // Read uint16_t buttons in big-endian
    outPacket.buttons = (static_cast<uint16_t>(static_cast<unsigned char>(buffer[0])) << 8) |
                       (static_cast<uint16_t>(static_cast<unsigned char>(buffer[1])));
    buffer += sizeof(uint16_t);
    outPacket.leftTrigger = ReadFloat(buffer); buffer += sizeof(float);
    outPacket.rightTrigger = ReadFloat(buffer); buffer += sizeof(float);
    outPacket.thumbLX = ReadFloat(buffer); buffer += sizeof(float);
    outPacket.thumbLY = ReadFloat(buffer); buffer += sizeof(float);
    outPacket.thumbRX = ReadFloat(buffer); buffer += sizeof(float);
    outPacket.thumbRY = ReadFloat(buffer);
    return true;
}

bool ParseScreenConfigPacket(const char* payload, size_t payloadSize, ScreenConfigPacket& outPacket) {
    // The size check should account for all fields in the packet.
    const size_t expectedSize = (sizeof(uint8_t) * 6 + sizeof(uint32_t) * 2 + sizeof(float));
    if (payloadSize < expectedSize) return false;
    const char* buffer = payload;
    outPacket.fps = static_cast<uint8_t>(*buffer); buffer++;
    outPacket.bitrate = ReadInt32(buffer); buffer += sizeof(int32_t);
    outPacket.scale = ReadFloat(buffer); buffer += sizeof(float);
    outPacket.quality = static_cast<uint8_t>(*buffer); buffer++;
    outPacket.method = static_cast<uint8_t>(*buffer); buffer++;
    outPacket.streamingMode = static_cast<uint8_t>(*buffer); buffer++;
    outPacket.minQuality = static_cast<uint8_t>(*buffer); buffer++;
    outPacket.clientBuffer = ReadInt32(buffer); buffer += sizeof(int32_t);
    outPacket.cursorEnabled = static_cast<uint8_t>(*buffer) != 0; buffer++;
    return true;
}

bool ParseTextInputPacket(const char* payload, size_t payloadSize, TextInputPacket& outPacket) {
    if (payloadSize < sizeof(uint16_t)) return false;
    const char* buffer = payload;
    outPacket.length = ReadInt16(buffer);
    return payloadSize == sizeof(uint16_t) + outPacket.length;
}

bool ParseCommandPacket(const char* payload, size_t payloadSize, CommandPacket& outPacket) {
    if (payloadSize < sizeof(uint16_t)) return false;
    const char* buffer = payload;
    outPacket.length = ReadInt16(buffer);
    return payloadSize == sizeof(uint16_t) + outPacket.length;
}

bool ParseKeycodeDownPacket(const char* payload, size_t payloadSize, KeycodePacket& outPacket) {
    if (payloadSize < sizeof(uint16_t)) return false;
    const char* buffer = payload;
    outPacket.count = ReadInt16(buffer);
    return payloadSize == sizeof(uint16_t) + outPacket.count * sizeof(uint32_t);
}

bool ParseKeycodeUpPacket(const char* payload, size_t payloadSize, KeycodePacket& outPacket) {
    if (payloadSize < sizeof(uint16_t)) return false;
    const char* buffer = payload;
    outPacket.count = ReadInt16(buffer);
    return payloadSize == sizeof(uint16_t) + outPacket.count * sizeof(uint32_t);
}

bool ParseHelloPacket(const char* payload, size_t payloadSize, HelloPacket& outPacket) {
    if (payloadSize < sizeof(HelloPacket)) return false;
    const char* buffer = payload;
    outPacket.version = ReadInt32(buffer); buffer += sizeof(uint32_t);
    outPacket.serviceFlags = ReadInt32(buffer);
    return true;
}

bool ParseHelloResponsePacket(const char* payload, size_t payloadSize, HelloResponsePacket& outPacket, std::vector<uint8_t>& outSalt) {
    if (payloadSize < sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint8_t)) return false;
    const char* buffer = payload;
    outPacket.capabilities = ReadInt32(buffer); buffer += sizeof(uint32_t);
    outPacket.version = ReadInt32(buffer); buffer += sizeof(uint32_t);
    outPacket.controllerPort = ReadInt16(buffer); buffer += sizeof(uint16_t);
    outPacket.screenPort = ReadInt16(buffer); buffer += sizeof(uint16_t);
    outPacket.serverId = ReadInt64(buffer); buffer += sizeof(uint64_t);
    outPacket.serverType = ReadInt16(buffer); buffer += sizeof(uint16_t);
    outPacket.passwordRequired = static_cast<uint8_t>(*buffer) != 0; buffer += sizeof(uint8_t);
    outPacket.saltLength = static_cast<uint8_t>(*buffer); buffer += sizeof(uint8_t);

    if (payloadSize < sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t) + outPacket.saltLength) return false;

    outSalt.assign(buffer, buffer + outPacket.saltLength);
    return true;
}

bool ParseAuthPacket(const char* payload, size_t payloadSize, AuthPacket& outPacket, std::vector<uint8_t>& outEncryptedData) {
    if (payloadSize < sizeof(uint16_t)) return false;
    const char* buffer = payload;
    outPacket.dataLength = ReadInt16(buffer); buffer += sizeof(uint16_t);
    
    if (payloadSize < sizeof(uint16_t) + outPacket.dataLength) return false;
    
    outEncryptedData.assign(buffer, buffer + outPacket.dataLength);
    return true;
}

bool ParseAuthResultPacket(const char* payload, size_t payloadSize, AuthResultPacket& outPacket) {
    if (payloadSize < sizeof(uint8_t) + sizeof(uint64_t)) return false;
    
    const char* buffer = payload;
    outPacket.result = static_cast<uint8_t>(*buffer); buffer += sizeof(uint8_t);
    
    // Read server ticks (8 bytes, little-endian)
    outPacket.serverTicks = 0;
    for (int i = 0; i < 8; ++i) {
        outPacket.serverTicks |= (static_cast<uint64_t>(static_cast<unsigned char>(buffer[i])) << (i * 8));
    }
    
    return true;
}


} // namespace Controller