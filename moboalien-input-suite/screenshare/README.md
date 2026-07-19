# Screen Sharing Module

This module adds screen capture and streaming capabilities to the Moboalien Input Suite.

## Features

- **Real-time screen capture** using Windows Desktop Duplication API
- **H.264 hardware encoding** via Windows Media Foundation
- **HTTP streaming** for compatibility with VLC and other media players
- **Configurable quality settings** (resolution, framerate, bitrate)
- **Integration with input server** for remote control

## Components

- `screen_capture.h/cpp` - Core screen capture and encoding
- `http_streamer.h/cpp` - HTTP streaming implementation
- `screen_server.cpp` - Standalone screen sharing server
- `screen_client.cpp` - Client utility for remote control
- `screen_integration.h/cpp` - Integration with input_server

## Usage

### Standalone Server
```bash
screen_server.exe --width 1920 --height 1080 --fps 30 --bitrate 2000 --port 8554
```

### Via Input Server
```bash
# Start input server
input_server.exe

# From another machine, start screen sharing
screen_client.exe 192.168.1.100 16234 start --width 1280 --height 720 --fps 60

# Stop screen sharing
screen_client.exe 192.168.1.100 16234 stop
```

### Connect with VLC
```
http://server_ip:8080/stream
```

## Configuration Options

- `--width` - Video width in pixels (default: 1920)
- `--height` - Video height in pixels (default: 1080)  
- `--fps` - Frame rate (default: 30)
- `--bitrate` - Bitrate in kbps (default: 2000)
- `--port` - HTTP port (default: 8080)

## Requirements

- Windows 8+ (for Desktop Duplication API)
- DirectX 11 compatible graphics
- Hardware H.264 encoder (recommended)

## Protocol

The screen sharing uses HTTP streaming for video delivery. This provides good compatibility with VLC and other media players while maintaining simplicity and low latency.