// SPDX-License-Identifier: GPL-3.0-or-later
// The controller-button remap choices: see pad_choices.h.
#include "pad_choices.h"

namespace nxrt::play {

uint64_t pad_choice_value(int c) {
    if (c == 0) return kPadDefault;
    if (c == 1) return 0;
    return static_cast<uint64_t>(hid::btn(kSwitchButtons[c - 2].bit));
}

int pad_choice_of(uint64_t v) {
    for (int c = 0; c < kPadChoiceCount; ++c)
        if (pad_choice_value(c) == v) return c;
    return -1;
}

}  // namespace nxrt::play
