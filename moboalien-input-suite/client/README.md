# Client Library

This directory contains client-side functionality for connecting to the Winlator input server.

## Structure

- `include/` - Client-side header files
- `handshake_client.cpp` - Client handshake implementation
- `include/handshake_client.h` - Client handshake interface

## Purpose

The client library provides functionality for:
- Performing handshake with the server
- Authentication
- Dynamic port discovery
- Protocol negotiation

## Dependencies

The client library depends on:
- `controller_protocol_lib` - Packet encoding/decoding
- `platform_lib` - Platform abstraction
- `crypto_lib` - Encryption/decryption

## Usage

```cpp
#include "handshake_client.h"

// Create platform instance
auto platform = CreatePlatform();

// Create handshake client
Controller::HandshakeClient handshakeClient(std::shared_ptr<Platform>(platformPtr.release()));

// Perform handshake
auto result = handshakeClient.PerformHandshake(serverIP, handshakePort, password);

if (result.success) {
    // Use dynamic ports for communication
    uint16_t controllerPort = result.controllerPort;
    uint16_t screenPort = result.screenPort;
}
```
