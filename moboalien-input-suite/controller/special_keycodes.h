#pragma once

// Special virtual key codes for mouse functionality
// Using range 0x1000-0x1FFF to avoid conflicts with Windows VK codes (0x01-0xFF)
#define VK_MOUSE_LEFT_BUTTON    0x1001
#define VK_MOUSE_RIGHT_BUTTON   0x1002
#define VK_MOUSE_MIDDLE_BUTTON  0x1003
#define VK_MOUSE_X1_BUTTON      0x1004
#define VK_MOUSE_X2_BUTTON      0x1005
#define VK_MOUSE_MOVE_X         0x1010
#define VK_MOUSE_MOVE_Y         0x1011
#define VK_MOUSE_WHEEL          0x1020
#define VK_LSHIFT               0xA0
#define VK_RSHIFT               0xA1
#define VK_LCONTROL             0xA2
#define VK_RCONTROL             0xA3
#define VK_LMENU                0xA4
#define VK_RMENU                0xA5
#define VK_SHIFT                0x10
#define VK_CONTROL              0x11
#define VK_MENU                 0x12

// Check if a virtual key code is a special mouse code
inline bool IsMouseButtonCode(int vk) {
    return vk >= VK_MOUSE_LEFT_BUTTON && vk <= VK_MOUSE_X2_BUTTON;
}

inline bool IsMouseMovementCode(int vk) {
    return vk == VK_MOUSE_MOVE_X || vk == VK_MOUSE_MOVE_Y;
}

inline bool IsMouseWheelCode(int vk) {
    return vk == VK_MOUSE_WHEEL;
}

// Check if a virtual key code is any mouse code that is not a on/off button
inline bool IsSpecialMouseCode(int vk) {
    return  IsMouseMovementCode(vk) || IsMouseWheelCode(vk);
}

inline bool IsModifierKey(int vk) {
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU;
}