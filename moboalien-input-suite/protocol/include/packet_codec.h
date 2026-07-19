#pragma once
#include <vector>
#include <string>
#include "packet_types.h" // For MousePacket, XInputPacket, etc.

namespace Controller {

//=============================================================================
// Packet Encoding Functions
//=============================================================================

std::vector<char> CreateConfigPacket(const std::vector<int>& buttonCodes);
std::vector<char> CreateStatePacket(const std::vector<float>& states);
std::vector<char> CreateMousePacket(float x, float y, float wheel, uint32_t buttons);
std::vector<char> CreateMouseConfigPacket(uint8_t mode);
std::vector<char> CreateXInputPacket(int controllerIndex, uint16_t buttons, float lt, float rt, float lx, float ly, float rx, float ry);
std::vector<char> CreateScreenConfigPacket(const ScreenConfigPacket& config);
std::vector<char> CreateTextInputPacket(const char* text, uint16_t length);
std::vector<char> CreateCommandPacket(const char* command, uint16_t length);
std::vector<char> CreateKeycodeDownPacket(const uint32_t* keycodes, uint16_t count);
std::vector<char> CreateKeycodeUpPacket(const uint32_t* keycodes, uint16_t count);
std::vector<char> CreateHelloPacket(uint32_t version, uint32_t serviceFlags);
std::vector<char> CreateHelloResponsePacket(uint32_t capabilities, uint32_t version, uint16_t controllerPort, uint16_t screenPort, uint64_t serverId, uint16_t serverType, bool passwordRequired, const std::vector<uint8_t>& salt);
std::vector<char> CreateAuthPacket(const std::vector<uint8_t>& encryptedData);
std::vector<char> CreateAuthResultPacket(bool success, int64_t serverTicks);

//=============================================================================
// Packet Deserialization Functions
//=============================================================================

bool ParseConfigPacket(const char* payload, size_t payloadSize, std::vector<int>& outButtonCodes);
bool ParseStatePacket(const char* payload, size_t payloadSize, size_t expectedCount, std::vector<float>& outStates);
bool ParseMousePacket(const char* payload, size_t payloadSize, MousePacket& outPacket);
bool ParseMouseConfigPacket(const char* payload, size_t payloadSize, MouseConfigPacket& outPacket);
bool ParseXInputPacket(const char* payload, size_t payloadSize, XInputPacket& outPacket);
bool ParseScreenConfigPacket(const char* payload, size_t payloadSize, ScreenConfigPacket& outPacket);
bool ParseTextInputPacket(const char* payload, size_t payloadSize, TextInputPacket& outPacket);
bool ParseCommandPacket(const char* payload, size_t payloadSize, CommandPacket& outPacket);
bool ParseKeycodeDownPacket(const char* payload, size_t payloadSize, KeycodePacket& outPacket);
bool ParseKeycodeUpPacket(const char* payload, size_t payloadSize, KeycodePacket& outPacket);
bool ParseHelloPacket(const char* payload, size_t payloadSize, HelloPacket& outPacket);
bool ParseHelloResponsePacket(const char* payload, size_t payloadSize, HelloResponsePacket& outPacket, std::vector<uint8_t>& outSalt);
bool ParseAuthPacket(const char* payload, size_t payloadSize, AuthPacket& outPacket, std::vector<uint8_t>& outEncryptedData);
bool ParseAuthResultPacket(const char* payload, size_t payloadSize, AuthResultPacket& outPacket);

} // namespace Controller