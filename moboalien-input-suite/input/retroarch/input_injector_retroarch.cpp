#include "i_input_injector.h"
#include "logger.h"
#include <cstdint>
#include <memory>
#include <string>

static const char* TAG = "InputInjectorRetroArch";

extern "C" void moboalien_inject_key(int port, int keycode, int down);
extern "C" void moboalien_inject_keyboard_event(int retrok, int down);
extern "C" void moboalien_inject_light_gun_buttons(int port, int keycode, int down);
extern "C" void moboalien_command_event(int cmd);
extern "C" void moboalien_inject_mouse_move(int port, int x, int y, int is_absolute);
extern "C" void moboalien_inject_mouse_button(int port, int button, int down);
extern "C" void moboalien_inject_mouse_wheel(int port, int delta);

/* --- MoboAlien VK → joypad mapping (remove when no longer needed) --- */
static int vk_to_joypad(int vk)
{
    switch (vk)
    {
        case  97: return 8;  /* RETRO_DEVICE_ID_JOYPAD_A      */
        case  96: return 0;  /* RETRO_DEVICE_ID_JOYPAD_B      */
        case 100: return 9;  /* RETRO_DEVICE_ID_JOYPAD_X      */
        case  99: return 1;  /* RETRO_DEVICE_ID_JOYPAD_Y      */
        case 108: return 3;  /* RETRO_DEVICE_ID_JOYPAD_START  */
        case 109: return 2;  /* RETRO_DEVICE_ID_JOYPAD_SELECT */
        case  19: return 4;  /* RETRO_DEVICE_ID_JOYPAD_UP     */
        case  20: return 5;  /* RETRO_DEVICE_ID_JOYPAD_DOWN   */
        case  21: return 6;  /* RETRO_DEVICE_ID_JOYPAD_LEFT   */
        case  22: return 7;  /* RETRO_DEVICE_ID_JOYPAD_RIGHT  */
        case 102: return 10; /* RETRO_DEVICE_ID_JOYPAD_L      */
        case 103: return 11; /* RETRO_DEVICE_ID_JOYPAD_R      */
        case 104: return 12; /* RETRO_DEVICE_ID_JOYPAD_L2     */
        case 105: return 13; /* RETRO_DEVICE_ID_JOYPAD_R2     */
        case 106: return 14; /* RETRO_DEVICE_ID_JOYPAD_L3     */
        case 107: return 15; /* RETRO_DEVICE_ID_JOYPAD_R3     */
        default:  return -1;
    }
}

static int vk_to_command(int virtualKey)
{
    switch (virtualKey)
    {
        case 282: return 1;  /* CMD_EVENT_MENU_TOGGLE */
        case 283: return 2;  /* CMD_EVENT_RESET */
        case 284: return 3;  /* CMD_EVENT_SAVE_STATE */
        case 285: return 4;  /* CMD_EVENT_LOAD_STATE */
        case 286: return 5;  /* CMD_EVENT_QUIT */
        case 287: return 6;  /* CMD_EVENT_TAKE_SCREENSHOT */
        case 288: return 7;  /* CMD_EVENT_STATISTICS_TOGGLE */
        case 289: return 8;  /* CMD_EVENT_PAUSE_TOGGLE */
        case 290: return 9;  /* CMD_EVENT_AUDIO_MUTE_TOGGLE */
    }
    return -1;
}

static int vk_to_lightgun(int virtualKey)
{
    switch (virtualKey)
    {
        case 301: return 1;  /* LIGHTGUN_TRIGGER */
        case 302: return 2;  /* LIGHTGUN_RELOAD */
        case 303: return 3;  /* LIGHTGUN_AUX_A */
        case 304: return 4;  /* LIGHTGUN_AUX_B */
        case 305: return 5;  /* LIGHTGUN_START */
        case 306: return 6;  /* LIGHTGUN_SELECT */
        case 307: return 7;  /* LIGHTGUN_AUX_C */
        case 308: return 8;  /* LIGHTGUN_DPAD_UP */
        case 309: return 9;  /* LIGHTGUN_DPAD_DOWN */
        case 310: return 10;  /* LIGHTGUN_DPAD_LEFT */
        case 311: return 11;  /* LIGHTGUN_DPAD_RIGHT */
        default: return -1;
    }
    return -1;
}

static bool is_command_key(int key)
{
    return key>=282 && key<=292;
}

static bool is_lightgun_button(int key)
{
    return key>=301 && key<=320;
}

class InputInjectorRetroArch final : public IInputInjector {
public:
    void SendKeyDown(int virtualKey, int port) override {
        LOGD(TAG, "SendKeyDown: " + std::to_string(virtualKey) + " port: " + std::to_string(port));
        if (is_command_key(virtualKey))
            moboalien_command_event(vk_to_command(virtualKey));
        else if (is_lightgun_button(virtualKey))
        {
            moboalien_inject_light_gun_buttons(port, vk_to_lightgun(virtualKey), 1);
        }
        else
        {
            int joypad_id = vk_to_joypad(virtualKey);
            if (joypad_id >= 0)
                moboalien_inject_key(port, joypad_id, 1);
        }
    }

    void SendKeyUp(int virtualKey, int port) override {
        LOGD(TAG, "SendKeyUp: " + std::to_string(virtualKey) + " port: " + std::to_string(port));
        if (is_command_key(virtualKey))
            return; /* toggle fires on down only */
        else if (is_lightgun_button(virtualKey))
        {
            moboalien_inject_light_gun_buttons(port, vk_to_lightgun(virtualKey), 0);
        }
        else
        {
            int joypad_id = vk_to_joypad(virtualKey);
            if (joypad_id >= 0)
                moboalien_inject_key(port, joypad_id, 0);
        }
    }

    void SendMouseMove(int deltaX, int deltaY, int port) override {
        moboalien_inject_mouse_move(port, deltaX, deltaY, 0);
    }

    void SendMouseMoveAbsolute(int x, int y, int port) override {
        moboalien_inject_mouse_move(port, x, y, 1);
    }

    void SendMouseButtonDown(int button, int port) override {
        moboalien_inject_mouse_button(port, button, 1);
    }

    void SendMouseButtonUp(int button, int port) override {
        moboalien_inject_mouse_button(port, button, 0);
    }

    void SendMouseWheel(int wheelDelta, int port) override {
        moboalien_inject_mouse_wheel(port, wheelDelta);
    }

    void SendTextInput(const char* text, uint16_t length, int port, bool async) override {
        // Not implemented for RetroArch yet
    }

    bool IsAvailable()                                  override { return true; }
    void SetMethod(InputMethod)                         override {}
    void SetMouseMethod(MouseMethod)                    override {}
};

std::unique_ptr<IInputInjector> CreateInputInjector() {
    return std::make_unique<InputInjectorRetroArch>();
}
