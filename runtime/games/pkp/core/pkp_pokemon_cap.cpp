// SPDX-License-Identifier: GPL-3.0-or-later
// Pokopia v2.0.1a: change only the two resident parameter limits (25/30 -> 50/55).
// This does NOT expand model resources or guarantee visibility of all 50 Pokemon.
#include "core/pkp_pokemon_cap.h"

#include <cstdint>

#include "core/pkp_guest.h"
#include "core/pkp_layout.h"
#include "core/pkp_settings.h"
#include "nxrt/log.h"

namespace nxrt::games::pkp {
namespace {

constexpr std::uint64_t kParamsLink = 0x20;
constexpr std::uint64_t kLivingOffset = 0x4bc;
constexpr std::uint64_t kObjectsOffset = 0x4c0;
constexpr std::uint32_t kOriginalLiving = 25;
constexpr std::uint32_t kOriginalObjects = 30;
constexpr std::uint32_t kTargetLiving = 50;
constexpr std::uint32_t kTargetObjects = 55;

struct State {
    std::uint64_t base = 0;
    std::uint64_t observed_params = 0;
    bool enabled = false;
    bool reported = false;
    bool warned = false;
};
State g;

void game_frame(::Ctx*) {
    if (!g.enabled || !g.base) return;

    // All addresses are v2.0.1a guest offsets, not process-native pointers.
    const std::uint64_t root = g.base + layout::kPokemonParamsRoot;
    if (!guest::ok(root, sizeof(std::uint64_t))) return;
    const std::uint64_t holder = guest::rd<std::uint64_t>(root);
    if (!guest::ok(holder, kParamsLink + sizeof(std::uint64_t))) return;
    const std::uint64_t params = guest::rd<std::uint64_t>(holder + kParamsLink);
    if (!guest::ok(params, kObjectsOffset + sizeof(std::uint32_t))) return;

    if (params != g.observed_params) {
        g.observed_params = params;
        g.reported = false;
        g.warned = false;
    }

    const std::uint32_t living = guest::rd<std::uint32_t>(params + kLivingOffset);
    const std::uint32_t objects = guest::rd<std::uint32_t>(params + kObjectsOffset);

    // Refuse unexpected layouts instead of treating arbitrary memory as counters.
    // Include the two mixed states in case one field is reinitialized independently.
    const bool expected_living = living == kOriginalLiving || living == kTargetLiving;
    const bool expected_objects = objects == kOriginalObjects || objects == kTargetObjects;
    if (!expected_living || !expected_objects) {
        if ((living != 0 || objects != 0) && !g.warned) {
            NXLOG_W("pkp", "pokemon cap: unexpected resident limits (%u/%u), leaving them unchanged",
                    static_cast<unsigned>(living), static_cast<unsigned>(objects));
            g.warned = true;
        }
        return;
    }

    if (living != kTargetLiving) guest::wr<std::uint32_t>(params + kLivingOffset, kTargetLiving);
    if (objects != kTargetObjects) guest::wr<std::uint32_t>(params + kObjectsOffset, kTargetObjects);
    if (!g.reported) {
        NXLOG_I("pkp", "pokemon cap: applied 50 living / 55 objects (previous %u/%u)",
                static_cast<unsigned>(living), static_cast<unsigned>(objects));
        g.reported = true;
    }
}

void install(const FeatureInstall& in) {
    g = {};
    g.base = in.base;
    g.enabled = settings::get_bool(settings::Key::PokemonCap50);
    NXLOG_I("pkp", "pokemon cap: %s (50 living / 55 objects)", g.enabled ? "enabled" : "disabled");
}

void remove() { g = {}; }

} // namespace

const Feature kPokemonCapFeature = {
    .name = "pokemon_cap",
    .install = &install,
    .remove = &remove,
    .game_frame = &game_frame,
};

} // namespace nxrt::games::pkp
