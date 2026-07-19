#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include "packet_codec.h"
#include "video_config.h"

#pragma comment(lib, "ws2_32.lib")


void sendScreenCommand(const char* serverIP, int serverPort, bool start, const CaptureConfig* captureConfig = NULL, const StreamerConfig* streamerConfig = NULL) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }
    
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverIP, &serverAddr.sin_addr);
    
    std::vector<char> packet;
    
    if (start) {
        ScreenConfigPacket config;
        config.fps = streamerConfig->fps;
        config.bitrate = streamerConfig->bitrate / 1000; // kbps
        config.scale = captureConfig->scale;
        config.quality = streamerConfig->adaptiveQuality.quality;
        config.method = (captureConfig->method == CAPTURE_DESKTOP_DUPLICATION); // 1 for DD, 0 for GDI
        config.streamingMode = (streamerConfig->streamingMode == STREAMING_DIFFERENTIAL); // 1 for diff, 0 for mjpeg
        config.minQuality = streamerConfig->adaptiveQuality.minQuality;
        config.clientBuffer = streamerConfig->maxClientBufferSize / 1024; // KB
        packet = Controller::CreateScreenConfigPacket(config);
        
        std::cout << "Sending screen share start command: "
                  << " @" << (int)config.fps << "fps, " << config.bitrate << "kbps"<< std::endl;
    } else {
        // Create a simple packet with just the type
        packet.resize(1);
        packet[0] = PACKET_SCREEN_STOP;
        std::cout << "Stopping screen share" << std::endl;
    }
    
    sendto(sock, packet.data(), packet.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
    
    closesocket(sock);
    WSACleanup();
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cout << "Screen Share Client - Winlator Input Suite\n"
                  << "Usage: screen_client <server_ip> <server_port> <start|stop> [options]\n"
                  << "Options for 'start' command:\n"
                  << "  --fps <fps>         Frame rate (default: 30)\n"
                  << "  --bitrate <kbps>    Bitrate in kbps (default: 2000)\n"
                  << "  --port <port>       Server port (default: 8080)\n"
                  << "  --scale <factor>    Resolution scale (default: 1.0, 0.5=half)\n"
                  << "  --quality <1-100>   JPEG quality (default: 75)\n"
                  << "  --method <gdi|dd>   Capture method: gdi or dd (default: gdi)\n"
                  << "  --streaming <mjpeg|diff> Streaming mode: mjpeg or diff (default: mjpeg)\n"
                  << "  --minquality <1-100> Min quality threshold (default: 30)\n"
                  << "  --clientbuffer <kb> Max per-client send buffer in KB (default: 5120)\n\n"
                  << "Examples:\n"
                  << "  screen_client 192.168.1.100 16234 start\n"
                  << "  screen_client 192.168.1.100 16234 start --scale 0.5 --fps 60 --streaming diff\n"
                  << "  screen_client 192.168.1.100 16234 stop\n";
        return 1;
    }
    
    const char* serverIP = argv[1];
    int serverPort = atoi(argv[2]);
    std::string command = argv[3];
    
    if (command == "start") {
        CaptureConfig captureConfig;
        StreamerConfig streamerConfig;
        
        // Parse additional options
        for (int i = 4; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--fps" && i + 1 < argc) {
                streamerConfig.fps = atoi(argv[++i]);
            }
            else if (arg == "--bitrate" && i + 1 < argc) {
                streamerConfig.bitrate = atoi(argv[++i]) * 1000; // Convert kbps to bps
            }
            else if (arg == "--port" && i + 1 < argc) {
                streamerConfig.port = static_cast<uint16_t>(atoi(argv[++i]));
            }
            else if (arg == "--scale" && i + 1 < argc) {
                captureConfig.scale = (float)atof(argv[++i]);
            }
            else if (arg == "--quality" && i + 1 < argc) {
                streamerConfig.adaptiveQuality.quality = atoi(argv[++i]);
            }
            else if (arg == "--method" && i + 1 < argc) {
                std::string method = argv[++i];
                if (method == "dd" || method == "desktop" || method == "duplication") {
                    captureConfig.method = CAPTURE_DESKTOP_DUPLICATION;
                } else {
                    captureConfig.method = CAPTURE_GDI;
                }
            }
            else if (arg == "--minquality" && i + 1 < argc) {
                streamerConfig.adaptiveQuality.minQuality = atoi(argv[++i]);
            }
            else if (arg == "--clientbuffer" && i + 1 < argc) {
                streamerConfig.maxClientBufferSize = atoi(argv[++i]) * 1024; // Convert KB to bytes
            }
        }
        
        sendScreenCommand(serverIP, serverPort, true, &captureConfig, &streamerConfig);
    }
    else if (command == "stop") {
        sendScreenCommand(serverIP, serverPort, false);
    }
    else {
        std::cerr << "Invalid command. Use 'start' or 'stop'" << std::endl;
        return 1;
    }
    
    return 0;
}