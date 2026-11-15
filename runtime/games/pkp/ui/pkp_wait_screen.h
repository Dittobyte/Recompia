// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia: a waiting screen over the game's picture, for the moments a feature takes the player somewhere or holds them
// until something outside their own game is ready (split screen: a visitor on its way to the first player). It covers the whole
// picture and says in two lines what is going on. See pkp_wait_screen.cpp.
#pragma once
#include "core/pkp_feature.h"

namespace nxrt::games::pkp {

namespace wait_screen {

// Covers the picture until hide(), and at the latest after `seconds`: a caller that never comes back does not leave a covered game.
// A second show replaces the first. Any thread.
void show(const char* title, const char* text, double seconds);
void hide();

}  // namespace wait_screen

extern const Feature kWaitScreenFeature;

}  // namespace nxrt::games::pkp
