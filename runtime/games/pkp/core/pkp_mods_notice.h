// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia: which gameplay mods are effectively on (pkp_mods_notice.cpp). A read-only snapshot of the [pkp] switches as
// settings::get_bool returns them (env, then the ini, then the table default), plus the one line it writes to pkp.log at start.
#pragma once
#include <string>
#include <vector>

#include "core/pkp_feature.h"

namespace nxrt::games::pkp::mods_notice {

enum class Kind { Gameplay, Experimental };

struct Entry {
    const char* key;      // the [pkp] key
    const char* label;    // player-facing name
    Kind kind;
    bool on;              // effectively on now
};

// Every row of the settings table marked as a gameplay mod, then menu_in_game and taller_fov. Writes nothing.
std::vector<Entry> snapshot();
// One list per kind. wording: "Enabled gameplay mods: Camera aim, Craft search." / "none"; never empty.
std::string text(const std::vector<Entry>& entries, Kind kind);

}  // namespace nxrt::games::pkp::mods_notice

namespace nxrt::games::pkp {

// Logs the snapshot once per start. Has no setting of its own and no hooks.
extern const Feature kModsNoticeFeature;

}  // namespace nxrt::games::pkp
