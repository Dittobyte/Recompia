// SPDX-License-Identifier: GPL-3.0-or-later
// The controller-button remap lists, shared by the F1 Controls page (play_ui_controls.cpp) and the in-game Controller tab
// (games/pkp/ui/pkp_options.cpp): the Switch buttons, the choices a controller button can press, and a label for each PadSrc.
// Labels read "Xbox / PlayStation (position)", with the Steam Controller's own names where they differ; each fits the in-game value box.
#pragma once
#include <cstdint>

#include "nxrt/input_source.h"
#include "play_types.h"

namespace nxrt::play {

struct SwitchButton {
    const char* name;
    hid::NpadButtonBit bit;
};
inline constexpr SwitchButton kSwitchButtons[] = {
    {"A", hid::kBtnA},         {"B", hid::kBtnB},         {"X", hid::kBtnX},         {"Y", hid::kBtnY},
    {"L", hid::kBtnL},         {"R", hid::kBtnR},         {"ZL", hid::kBtnZL},       {"ZR", hid::kBtnZR},
    {"PLUS", hid::kBtnPlus},   {"MINUS", hid::kBtnMinus}, {"UP", hid::kBtnUp},       {"DOWN", hid::kBtnDown},
    {"LEFT", hid::kBtnLeft},   {"RIGHT", hid::kBtnRight}, {"LSTICK", hid::kBtnStickL}, {"RSTICK", hid::kBtnStickR},
};
inline constexpr int kSwitchButtonCount = static_cast<int>(sizeof kSwitchButtons / sizeof kSwitchButtons[0]);
static_assert(kSwitchButtonCount == 16, "the Switch pad has 16 buttons in the status view");

// The 24 controller buttons, in PadSrc order.
inline constexpr const char* kPadSrcLabels[kPadSrcCount] = {
    "A / Cross (bottom)", "B / Circle (right)", "X / Square (left)", "Y / Triangle (top)",
    "LB / L1",            "RB / R1",            "LT / L2",            "RT / R2",
    "Menu / Options",     "View / Create",      "Left stick click",   "Right stick click",
    "D-pad up",           "D-pad down",         "D-pad left",         "D-pad right",
    "Right grip R4 (upper)", "Left grip L4 (upper)", "Right grip R5 (lower)", "Left grip L5 (lower)",
    "Touchpad / left trackpad click", "Right trackpad click",
    "Guide (Steam button)", "Extra (quick access / Share)",
};
static_assert(sizeof kPadSrcLabels / sizeof kPadSrcLabels[0] == 24, "one label per controller button");

// The choices of a controller button: Default, Nothing, then the single Switch buttons in kSwitchButtons order.
inline constexpr const char* kPadChoiceNames[] = {
    "Default", "Nothing",
    "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "Plus", "Minus",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Left stick click", "Right stick click",
};
inline constexpr int kPadChoiceCount = static_cast<int>(sizeof kPadChoiceNames / sizeof kPadChoiceNames[0]);
static_assert(kPadChoiceCount == 2 + kSwitchButtonCount, "one choice per Switch button, plus Default and Nothing");
inline constexpr const char* kPadCustomLabel = "Custom (edit in controls.ini)";

uint64_t pad_choice_value(int c);   // the remap value a choice stores (kPadDefault, 0 = nothing, or one Switch button)
int pad_choice_of(uint64_t v);      // the choice a stored value shows as, or -1 when none holds it (a mask of buttons set in the file)

}  // namespace nxrt::play
