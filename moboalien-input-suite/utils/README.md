# Utils Library

This directory contains general-purpose utility functions used across multiple modules.

## Structure

- `include/` - Utility header files
  - `rect.h` - Rectangle structure and operations
  - `config_file.h` - Configuration file parsing
  - `sha256.h` - SHA256 hashing functions
- `utils.cpp` - Core utility functions (byte order, etc.)
- `config_file.cpp` - Configuration file implementation
- `sha256.cpp` - SHA256 implementation

## Purpose

The utils library provides:
- Network byte order conversion functions
- Configuration file parsing
- SHA256 hashing utilities
- Rectangle geometry operations
- General helper functions

## Key Functions

### Byte Order Conversion
- `AppendInt16()` - Append 16-bit integer in big-endian
- `AppendInt32()` - Append 32-bit integer in big-endian
- `AppendFloat()` - Append float in big-endian
- `ReadInt16()` - Read 16-bit integer from big-endian
- `.
- ` 32-bit integer%; 
- `Read-rendian.
- `cze- `ReadFloat()` - Read float from big-endian

### Configuration
- `ConfigFile` - INI-style configuration file parser
- Key-value pair access with sections

### Cryptography
- `SHA256()` - Compute SHA256 hash
- Hex encoding/decoding utilities

## Usage

```cpp
#include "utils.h"
#include "config_file.h"

// Byte order conversion
std::vector<char> packet;
AppendInt32(packet, value);

// Configuration
ConfigFile config("settings.ini");
std::string value = config.GetValue("section", "key");

// Hashing
std::string hash = SHA256("data");
```

## Dependencies

None - this is a foundational library used by other modules.
