# Image Utils Library

This directory contains image processing and encoding utilities.

## Structure

- `include/` - Image utils header files
  - `i_image_encoder.h` - Image encoder interface
  - `utils.h` - Image utility functions
- `windows/` - Windows-specific implementation
  - `image_encoder.cpp` - Windows GDI+ image encoder

## Purpose

The image utils library provides:
- JPEG and PNG image encoding
- Image format conversion utilities
- Cross-platform image processing abstraction
- Platform-specific optimizations

## Key Classes

### IImageEncoder
Abstract interface for image encoding with methods:
- `EncodeToJPEG()` - Convert to JPEG format
- `EncodeToPNG()` - Convert to PNG format

### ImageEncoder (Windows)
Windows implementation using GDI+:
- Supports JPEG with quality control
- Supports PNG with transparency
- Handles RGBA to BGRA conversion
- Rectangle cropping support

## Usage

```cpp
#include "image_utils/i_image_encoder.h"

// Create encoder (platform-specific)
auto encoder = std::make_unique<ImageEncoder>();

// Encode JPEG
unsigned char* jpegData = nullptr;
size_t jpegSize = 0;
bool success = encoder->EncodeToJPEG(frame, &jpegData, &jpegSize, quality);

// Clean up
delete[] jpegData;
```

## Dependencies

- `platform_lib` - Platform abstraction
- Windows: `gdiplus` - GDI+ library
- `utils_lib` - Utility functions (rect.h)
