#pragma once
#include <string>
#include <unordered_map>
#include "packet_types.h"
#include "server_context.h"
#include "i_input_injector.h"

// Packet handler functions with dependency injection
void handleConfigPacket(ServerContext& ctx, Platform* platform, const std::string& clientKey, const char* payload, size_t payloadSize);
void handleStatePacket(ServerContext& ctx, Platform* platform, const std::string& clientKey, const char* payload, size_t payloadSize);
void handleMousePacket(ServerContext& ctx, IInputInjector* inputInjector, const std::string& clientKey, const char* payload, size_t payloadSize);
void handleXInputPacket(ServerContext& ctx, Platform* platform, const char* payload, size_t payloadSize);
void handleScreenConfigPacket(ServerContext& ctx, Platform* platform, const char* payload, size_t payloadSize);
void handleScreenStopPacket(ServerContext& ctx, Platform* platform);
void handleTextInputPacket(ServerContext& ctx, IInputInjector* inputInjector, const std::string& clientKey, const char* payload, size_t payloadSize);
void handleCommandPacket(ServerContext& ctx, Platform* platform, const std::string& clientKey, const char* payload, size_t payloadSize);
void handleKeycodeDownPacket(ServerContext& ctx, IInputInjector* inputInjector, const std::string& clientKey, const char* payload, size_t payloadSize);
void handleKeycodeUpPacket(ServerContext& ctx, IInputInjector* inputInjector, const std::string& clientKey, const char* payload, size_t payloadSize);
void handleMouseConfigPacket(ServerContext& ctx, const char* payload, size_t payloadSize);