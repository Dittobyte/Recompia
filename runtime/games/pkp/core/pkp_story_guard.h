// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia: Story co-op's safety net ([pkp] link_story_coop, pkp_linkplay.h). With Story co-op a player's story also moves in
// other players' worlds, so two records are kept next to the save, both only while the setting is on:
// - a copy of the save as it was at start, before the game read it: <save dir>/story-coop-backup/<yyyymmdd-hhmmss>/ (the newest
//   kCopiesKept stay). Putting one back is copying its <key> folder and <key>.meta over <save dir>/account/ with the game closed;
// - a journal of every change to the story quest history (m_story's quest state and flag, pkp_linkplay.cpp):
//   <save dir>/story-journal/<key>.tsv, one line per change with the time, the player's role, the stage, the quest, what changed and the
//   value before and after. It says in whose world a step happened and what to set back to undo it.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/pkp_feature.h"

namespace nxrt::games::pkp {

constexpr size_t kStoryCopiesKept = 5;

// A story quest's two bytes in m_story: ids 1..800 at +0x1060 + 2 * id, the DLC's 801..1000 at +0x1860 + 2 * (id - 800) (the game's own
// index, 0x11e884). Pure: the offset of the state byte from m_story (the flag byte is the next one), 0 for an id out of range.
uint64_t story_quest_offset(uint32_t id);

// The registry's feature after linkplay and save_uuid (the copy holds the repaired save): install makes the copy and registers the
// journal's hooks; the frame callback's first call checks main and arms them or removes them. Logged.
extern const Feature kStoryGuardFeature;

}  // namespace nxrt::games::pkp
