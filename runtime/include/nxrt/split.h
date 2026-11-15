// SPDX-License-Identifier: GPL-3.0-or-later
// Split screen: the game runs once per player on one PC (two to four), each process a window in its part of the screen, and they meet
// through the game's own multiplayer on 127.0.0.1. Whoever starts the group (the launcher) tells each process which window it is; play
// mode then keeps the window in its part and gives it one controller (src/gfx/play/play_split.cpp), and the game layer can ask here
// which player it runs.
// After pokopia-native's split screen (MIT, windows/docs/split-screen.md): the same design, on this runtime's own window and input code.
//
// Configuration ([split], environment NXRT_SPLIT_<KEY>, read once; docs/CONFIG.md):
//   window = 0          1: the first window (it hosts), 2 to 4: another one (it visits); 0: no split screen
//   count = 2           how many windows share the screen: 2, 3 or 4
//   layout = stacked    two windows: stacked = top and bottom halves, each as wide as the screen; side = left and right halves.
//                       Three: the first window has the top half (stacked) or the left half (side), the other two share the rest.
//                       Four: a quarter each, whatever this says
//   pad = <window>      the controller this window plays, by its place in the order the controllers were connected (1 = the first);
//                       0: none (keyboard and mouse only)
//   mute = false        this window plays no sound
//   first_account =     windows 2 to 4: the folder name of the first player's account save (<save dir>/account/<name>, 32 hex digits), for a
//                       game layer whose save must differ from it. A name and not a path: both windows share the save folder, and an
//                       environment variable does not carry every path on Windows
#pragma once
#include <string>

namespace nxrt::split {

int window();                // 0: no split screen, else which window this process is (1 to 4)
std::string first_account(); // empty when the starter named none (or what it named is not 32 hex digits)

}  // namespace nxrt::split
