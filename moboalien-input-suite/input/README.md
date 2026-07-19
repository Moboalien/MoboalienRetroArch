# Input Library

This directory contains input handling and shared memory structures.

## Structure

- `include/` - Input header files
  - `i_input_injector.h` - Input injection interface
  - `shared_input.h` - Shared memory structures
- `windows/` - Windows-specific implementation
  - `input_injector.cpp` - Windows SendInput implementation
  - `sendinput.cpp` - SendInput utility functions

## Purpose

The input library provides:
- Cross-platform input injection abstraction
- Shared memory structures for input state
- Platform-specific input implementations
- Real-time input simulation

## Key Components

### Shared Memory Structures
- `SharedInput` - Main shared memory layout
- `KeyboardState` - 256-bit keyboard state mask
- `MouseState` - Mouse position, wheel, and buttons
- `XInputState` - Xbox controller state for 4 controllers

### IInputInjector Interface
Abstract methods for input injection:
- `SendKeyDown()` / `SendKeyUp()` - Keyboard events
- `SendMouseMove()` - Relative mouse movement
- `SendMouseMoveAbsolute()` - Absolute mouse positioning
- `SendMouseButtonDown()` / `SendMouseButtonUp()` - Mouse buttons
- `SendMouseWheel()` - Mouse wheel scrolling

### InputInjector (Windows)
Windows implementation using SendInput API:
- Zero-delay input injection
- Support for both scancode and virtual key modes
- Perfect timing for sigma-delta modulation
- Desktop access validation

## Usage

```cpp
#include "input/i_input_injector.h"

// Create injector
auto injector = std::make_unique<InputInjector>();

// Send keyboard input
injector->SendKeyDown(VK_SPACE);
injector->SendKeyUp(VK_SPACE);

// Send mouse input
injector->SendMouseMove(10, 5); // Relative
injector->SendMouseMoveAbsolute(100, 200); // Absolute
```

## Shared Memory

The library uses shared memory for inter-process communication:
- Magic number: 0x57494E50 ('WINP')
- Version: 1
- Platform-independent layout
- Atomic updates for consistency

## Dependencies

- `platform_lib` - Platform abstraction
- Windows: Windows API for SendInput
