// SPDX-License-Identifier: GPL-3.0-or-later
// Capability line for a newly opened gamepad (declarations in pad_caps.h). Only queries; no sensor, rumble or LED state is changed.
#include "pad_caps.h"

#include <cstdio>

#include "nxrt/log.h"

namespace nxrt::platform {
namespace {

constexpr const char* kTag = "platform";

// Paddle and misc buttons a pad reports, as a comma list ("-" when none). SDL_GamepadHasButton is a mapping query, not a press state.
void describe_buttons(SDL_Gamepad* pad, char* out, size_t cap) {
    static const struct {
        SDL_GamepadButton button;
        const char* name;
    } kButtons[] = {
        {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, "rp1"}, {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, "rp2"},
        {SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, "lp1"},  {SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, "lp2"},
        {SDL_GAMEPAD_BUTTON_TOUCHPAD, "tp"},       {SDL_GAMEPAD_BUTTON_MISC1, "m1"},
        {SDL_GAMEPAD_BUTTON_MISC2, "m2"},          {SDL_GAMEPAD_BUTTON_MISC3, "m3"},
        {SDL_GAMEPAD_BUTTON_MISC4, "m4"},          {SDL_GAMEPAD_BUTTON_MISC5, "m5"},
        {SDL_GAMEPAD_BUTTON_MISC6, "m6"},
    };
    size_t used = 0;
    out[0] = '\0';
    for (const auto& b : kButtons) {
        if (!SDL_GamepadHasButton(pad, b.button)) continue;
        const int w = std::snprintf(out + used, cap - used, "%s%s", used ? "," : "", b.name);
        if (w < 0 || static_cast<size_t>(w) >= cap - used) break;
        used += static_cast<size_t>(w);
    }
    if (used == 0) std::snprintf(out, cap, "-");
}

// Touchpads as "<count>:<fingers of pad 0>,<fingers of pad 1>..." (at most 4 listed); "0" when the pad has none.
void describe_touchpads(SDL_Gamepad* pad, char* out, size_t cap) {
    const int count = SDL_GetNumGamepadTouchpads(pad);
    size_t used = 0;
    const int w = std::snprintf(out, cap, "%d", count < 0 ? 0 : count);
    if (w < 0 || static_cast<size_t>(w) >= cap) return;
    used = static_cast<size_t>(w);
    for (int i = 0; i < count && i < 4; ++i) {
        const int fingers = SDL_GetNumGamepadTouchpadFingers(pad, i);
        const int n = std::snprintf(out + used, cap - used, "%s%d", i ? "," : ":", fingers);
        if (n < 0 || static_cast<size_t>(n) >= cap - used) return;
        used += static_cast<size_t>(n);
    }
}

}  // namespace

void log_pad_caps(SDL_JoystickID id, SDL_Gamepad* pad) {
    if (!pad) return;
    const SDL_PropertiesID props = SDL_GetGamepadProperties(pad);
    const auto cap = [props](const char* key) { return props != 0 && SDL_GetBooleanProperty(props, key, false) ? 1 : 0; };
    int percent = -1;  // SDL leaves -1 when the level is unknown
    const SDL_PowerState power = SDL_GetGamepadPowerInfo(pad, &percent);
    char touch[48];
    char buttons[64];
    describe_touchpads(pad, touch, sizeof touch);
    describe_buttons(pad, buttons, sizeof buttons);
    NXLOG_I(kTag,
            "gamepad %u caps: type=%d real_type=%d vid=%04x pid=%04x player=%d power=%d percent=%d touchpads=%s mono_led=%d rgb_led=%d "
            "player_led=%d rumble=%d trigger_rumble=%d buttons=%s",
            static_cast<unsigned>(id), static_cast<int>(SDL_GetGamepadType(pad)), static_cast<int>(SDL_GetRealGamepadType(pad)),
            static_cast<unsigned>(SDL_GetGamepadVendor(pad)), static_cast<unsigned>(SDL_GetGamepadProduct(pad)),
            SDL_GetGamepadPlayerIndex(pad), static_cast<int>(power), percent, touch, cap(SDL_PROP_GAMEPAD_CAP_MONO_LED_BOOLEAN),
            cap(SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN), cap(SDL_PROP_GAMEPAD_CAP_PLAYER_LED_BOOLEAN),
            cap(SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN), cap(SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN), buttons);
}

}  // namespace nxrt::platform
