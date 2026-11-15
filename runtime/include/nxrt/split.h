// SPDX-License-Identifier: GPL-3.0-or-later
// Split screen: the game runs twice on one PC, each process a window in its half of the screen, and the two meet through the game's own
// multiplayer on 127.0.0.1. Whoever starts the pair (the launcher) tells each process which window it is; play mode then keeps the
// window in its half and gives it one controller (src/gfx/play/play_split.cpp), and the game layer can ask here which player it runs.
// After pokopia-native's split screen (MIT, windows/docs/split-screen.md): the same design, on this runtime's own window and input code.
//
// Configuration ([split], environment NXRT_SPLIT_<KEY>, read once; docs/CONFIG.md):
//   window = 0          1: the first window (the first half, it hosts), 2: the second (it visits); 0: no split screen
//   layout = stacked    stacked: top and bottom halves, each as wide as the screen; side: left and right halves
//   pad = <window>      the controller this window plays, by its place in the order the controllers were connected (1 = the first);
//                       0: none (keyboard and mouse only)
//   mute = false        this window plays no sound
//   first_account =     window 2: the folder name of the first player's account save (<save dir>/account/<name>, 32 hex digits), for a
//                       game layer whose save must differ from it. A name and not a path: both windows share the save folder, and an
//                       environment variable does not carry every path on Windows
#pragma once
#include <string>

namespace nxrt::split {

int window();                // 0: no split screen, else which window this process is (1 or 2)
std::string first_account(); // empty when the starter named none (or what it named is not 32 hex digits)

}  // namespace nxrt::split
