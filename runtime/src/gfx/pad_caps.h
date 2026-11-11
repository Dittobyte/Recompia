// SPDX-License-Identifier: GPL-3.0-or-later
// What a connected gamepad offers, written to the log once when it is opened (platform.cpp). Read-only SDL queries: nothing is enabled or set.
#pragma once
#include <SDL3/SDL_gamepad.h>

namespace nxrt::platform {

// One info line "gamepad <id> caps: ...": type, ids, player index, battery, touchpads, LEDs, rumble, extra buttons.
void log_pad_caps(SDL_JoystickID id, SDL_Gamepad* pad);

}  // namespace nxrt::platform
