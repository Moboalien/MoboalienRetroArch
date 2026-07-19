/*
Cross-platform UDP client that sends button configs and floating point state arrays.
Uses direct key codes instead of string mapping.
*/


#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <memory>
#include "packet_types.h"
#include "utils.h"
#include "packet_codec.h"
#include "controller/key_mappings.h"
#include "platform.h"
#include "i_crypto_helper.h"
#include "handshake_client.h"

// Helper function to send encrypted or unencrypted packets
int sendPacket(const std::vector<char>& packet, 
              Controller::HandshakeClient& handshakeClient,
              Platform* platform,
              uintptr_t sock,
              const std::string& serverIP,
              uint16_t controllerPort) {
    
    auto encryptor = handshakeClient.GetPacketEncryptor();
    
    if (encryptor) {
        std::vector<uint8_t> packetData(packet.begin(), packet.end());
        auto encryptedData = encryptor->EncryptPacket(packetData);

        // Safety check: ensure encryption succeeded
        if (encryptedData.empty()) {
            std::cerr << "Packet encryption failed!" << std::endl;
            return -1;
        }
        
        int result = platform->SendTo(sock, reinterpret_cast<const char*>(encryptedData.data()), encryptedData.size(), 0, serverIP, controllerPort);
        return result;
    } else {
        int result = platform->SendTo(sock, packet.data(), packet.size(), 0, serverIP, controllerPort);
        return result;
    }
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    const char* serverIP = "127.0.0.1"; // Default to localhost
    int handshakePort = 16235;
    const char* password = "12345678"; // Default password
    
    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
            serverIP = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            handshakePort = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
            password = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: " << argv[0] << " --server <IP> --port <port> --password <password>" << std::endl;
            std::cout << "  --server: Server IP address (default: 127.0.0.1)" << std::endl;
            std::cout << "  --port: Handshake port (default: 16235)" << std::endl;
            std::cout << "  --password: Authentication password (default: 12345678)" << std::endl;
            return 0;
        }
    }
    
    // Create platform and initialize sockets
    auto platform = CreatePlatform();
    if (!platform->SocketsInitialize()) {
        std::cerr << "Socket initialization failed" << std::endl;
        return 1;
    }
    
    std::cout << "Starting handshake with " << serverIP << ":" << handshakePort << std::endl;
    
    // Create handshake client with platform dependency
    Controller::HandshakeClient handshakeClient(platform.get());
    auto handshakeResult = handshakeClient.PerformHandshake(serverIP, handshakePort, password);
    if (!handshakeResult.success) {
        std::cerr << "Handshake failed: " << handshakeResult.errorMessage << std::endl;
        platform->SocketsCleanup();
        return 1;
    }
    
    std::cout << "Authentication successful!" << std::endl;
    std::cout << "Controller port: " << handshakeResult.controllerPort << std::endl;
    std::cout << "Screen port: " << handshakeResult.screenPort << std::endl;
    std::cout << "Server ticks: " << handshakeResult.serverTicks << std::endl;

    // Create socket for main communication using dynamic port from server
    uintptr_t sock = platform->CreateUDPSocket(0);
    if (sock == Platform::INVALID_SOCKET_HANDLE) {
        std::cerr << "Socket creation failed" << std::endl;
        platform->SocketsCleanup();
        return 1;
    }

    std::cout << "UDP Input Client connected to " << serverIP << ":" << handshakeResult.controllerPort << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  CONFIG " << 0x41 << " " << 0x42 << " " << 0x57 << " " << 0x53 << " " << 0x44 << " " << 0x20 << std::endl;
    std::cout << "  STATE 0.8 0.0 0.3 0.0 0.0 0.1" << std::endl;
    std::cout << "  MOUSE 10.5 -5.2 120 1" << std::endl;
    std::cout << "  XINPUT 0 4096 0.5 0.8 -0.2 0.1 0.0 -0.9" << std::endl;

    std::vector<int> buttonCodes;
    std::string line;

    while (std::getline(std::cin, line)) {
        if (line == "quit") break;

        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "CONFIG") {
            buttonCodes.clear();
            int keyCode;
            while (ss >> keyCode) {
                buttonCodes.push_back(keyCode);
            }

            std::cout << "Creating CONFIG packet with " << buttonCodes.size() << " buttons..." << std::endl;

            // Check if packet encryptor is available
            auto encryptor = handshakeClient.GetPacketEncryptor();
            if (!encryptor) {
                std::cerr << "No packet encryptor available - handshake may have failed!" << std::endl;
                continue;
            }

            std::cout << "Encryptor available, creating packet..." << std::endl;
            std::vector<char> packet = Controller::CreateConfigPacket(buttonCodes);
            std::cout << "Packet created, size: " << packet.size() << " bytes" << std::endl;
            
            std::cout << "Sending packet..." << std::endl;
            int result = sendPacket(packet, handshakeClient, platform.get(), sock, serverIP, handshakeResult.controllerPort);
            if (result < 0) {
                std::cerr << "Failed to send CONFIG packet!" << std::endl;
            } else {
                std::cout << "Config sent: " << buttonCodes.size() << " buttons." << std::endl;
            }

        } else if (cmd == "STATE") {
            if (buttonCodes.empty()) {
                std::cout << "Send CONFIG first" << std::endl;
                continue;
            }

            std::vector<float> states;
            float val;
            while (ss >> val && states.size() < buttonCodes.size()) {
                states.push_back(val);
            }

            if (states.size() != buttonCodes.size()) {
                std::cout << "Expected " << buttonCodes.size() << " values" << std::endl;
                continue;
            }

            std::vector<char> packet = Controller::CreateStatePacket(states);
            sendPacket(packet, handshakeClient, platform.get(), sock, serverIP, handshakeResult.controllerPort);
            std::cout << "State sent" << std::endl;
            
        } else if (cmd == "MOUSE") {
            float x, y, wheel;
            uint32_t buttons;
            ss >> x >> y >> wheel >> buttons;
            
            std::vector<char> packet = Controller::CreateMousePacket(x, y, wheel, buttons);
            sendPacket(packet, handshakeClient, platform.get(), sock, serverIP, handshakeResult.controllerPort);
            std::cout << "Mouse sent: pos(" << x << "," << y << ") wheel=" << wheel << " buttons=0x" << std::hex << buttons << std::dec << std::endl;
            
        } else if (cmd == "XINPUT") {
            int controllerIndex;
            uint16_t buttons;
            float lt, rt, lx, ly, rx, ry;
            ss >> controllerIndex >> buttons >> lt >> rt >> lx >> ly >> rx >> ry;
            
            std::vector<char> packet = Controller::CreateXInputPacket(controllerIndex, buttons, lt, rt, lx, ly, rx, ry);
            sendPacket(packet, handshakeClient, platform.get(), sock, serverIP, handshakeResult.controllerPort);
            std::cout << "XInput sent: controller=" << controllerIndex << " buttons=0x" << std::hex << buttons << std::dec << std::endl;
        }
    }

    platform->CloseSocket(sock);
    platform->SocketsCleanup();
    return 0;
}