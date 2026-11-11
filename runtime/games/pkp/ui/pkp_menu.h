// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia, the mouse in the game's menus (pkp_menu.cpp): the pointer over the game's UI panes, hover moves the game's own cursor
// there, left click confirms, right click and the wheel are menu bindings (pkp_data.cpp). A feature of the registry; its frame answers
// are wired into play mode's GamePlay table by pkp_game.cpp.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/pkp_feature.h"
#include "play_game.h"

namespace nxrt::games::pkp {

extern const Feature kMenuFeature;
play::Act menu_frame(bool menu_up, const play::mm::Geometry& g);   // main thread, once per frame (play_game.h GamePlay::menu_frame)
bool menu_over_value();                                             // the pointer is over a slider: the wheel steps it
bool menu_active();                                                 // the menu hooks saw an active pointer this frame
bool menu_in_game(bool in_game);                                    // main thread, once per frame before the context: a menu is up over play

// The menu module's pane-draw hooks (the 12 draw sites of kDrawSites) are the only hooks on those addresses, so another module that needs to
// see each pane as it draws registers here instead of hooking them (PROJECT.md section 5: a small registration interface, as the key guide's
// Client). fn runs on the drawing thread at the start of every pane's draw, after the menu has recorded the pane. At most 4 clients.
using PaneDrawClient = void (*)(uint64_t pane);
void menu_add_pane_draw_client(PaneDrawClient fn);
void menu_remove_pane_draw_client(PaneDrawClient fn);
// For a client, drawing thread: the pane's drawn rectangle in canvas pixels (1920x1080, y down) from its world matrix, size and anchor.
bool menu_pane_rect(uint64_t pane, float* x0, float* y0, float* x1, float* y1);
// For a client, drawing thread: multiplies the pane's width and height (its size field) by k; the draw that follows uses the new size.
void menu_pane_scale(uint64_t pane, float k);

}  // namespace nxrt::games::pkp
