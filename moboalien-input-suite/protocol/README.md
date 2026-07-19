# Protocol Library

This directory contains shared protocol functionality used by both clients and servers.

## Structure

- `include/` - Protocol header files
  - `packet_types.h` - Packet structure definitions
  - `packet_codec.h` - Packet encoding/decoding functions
- `packet_codec.cpp` - Protocol implementation

## Purpose

The protocol library provides:
- Packet type definitions and structures
- Encoding/decoding functions for all packet types
- Network byte order conversion utilities
- Shared protocol constants

## Packet Types

- `PACKET_CONFIG` - Button configuration
- `PACKET_STATE` - Controller state updates
- `PACKET_MOUSE` - Mouse input
- `PACKET_XINPUT` - Xbox controller input
- `PACKET_HELLO` - Handshake initiation
- `PACKET_HELLO_RESPONSE` - Handshake response with ports
- `PACKET_AUTH` - Authentication data
- `PACKET_AUTH_RESULT` - Authentication result

## Usage

```cpp
#include "packet_codec.h"
#include "packet_types.h"

// Create a packet
std::vector<char> packet = Controller::CreateConfigPacket(buttonCodes);

// Parse a packet
MousePacket mouseData;
if (Controller::ParseMousePacket(payload, size, mouseData)) {
    // Use mouseData
}
```

## Dependencies

- `utils_lib` - For utility functions like byte order conversion
