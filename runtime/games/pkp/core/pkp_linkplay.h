// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia, Link Play "Interact everywhere" (pkp_linkplay.cpp, [pkp] link_interact_everywhere): an experimental opt-in that gives
// a Link Play session in the story areas Palette Town's Spectator-Off permissions (visitors can use moves and items, pick up, place and
// build). Every player of a session must have the same setting: it is sent as the ldn session variant (services_api.h ldnapi), so a peer
// whose setting differs is refused when it joins.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/pkp_feature.h"

namespace nxrt::games::pkp {

// One supported main build: the permission-mask builder `fn`, the site in it and the instruction word expected there (offsets in main).
struct InteractSite {
    const char* build;   // main's build id, lowercase hex (a prefix of Module::build_id())
    uint64_t fn, site;
    uint32_t word;
};
enum class InteractCheck { Ok, UnknownBuild, NotLifted, WordDiffers };

constexpr uint8_t kInteractVariant = 1;                   // the ldn session variant while the hook is armed
constexpr const char* kInteractName = "Interact everywhere";
// Story co-op's name in the co-op mods fingerprint (ldnapi::set_coop_mods): pokopia-native's COOP_STORY_MOD, byte for byte.
constexpr const char* kStoryName = "Story co-op (experimental)";

// Pure: the site of main build `build_id` (null when none is known).
const InteractSite* interact_site(const std::string& build_id);
// Pure: whether the hook may be armed for this main: a known build, the site lifted as its own function (the lifted libs carry the
// midhook) and the expected instruction word at it. *site: the build's site, or null.
InteractCheck interact_check(const std::string& build_id, bool site_lifted, uint32_t word, const InteractSite** site);
// Pure: the register change at the site (x = x0..x30): while the session's Spectator flag (x22 bit 0) is clear, the Spectator pattern in
// x13 becomes the normal online one (0x5ffffffe; the movk at the site then adds bit 32, online), or with keep_story that pattern plus bit 0
// (0x5fffffff: the story system stays on, for a Story co-op host). True when x13 changed.
bool interact_apply(uint64_t* x, bool keep_story = false);

// A feature of the registry (pkp_game.cpp): its install registers the midhook when [pkp] link_interact_everywhere is on (main is not
// loaded yet, so nothing is checked there); its frame callback's first call checks main (its build id, the lifted site, the instruction
// word) and arms the hook or removes it, through linkplay_arm(); logs once either way.
extern const Feature kLinkplayFeature;
// The decision of the frame callback with main's facts given (one per kStorySites entry of pkp_linkplay.cpp, in its order; tests): arms the hook and sets the ldn session variant on Ok, else removes
// the hook and clears the variant. Returns the check. Does nothing (returns Ok) when the hook was not installed.
InteractCheck linkplay_arm(const std::string& build_id, bool site_lifted, uint32_t word);
// Story co-op ([pkp] link_story_coop, pokopia-native docs/story-coop.md), hooks of its own:
//   - the talk site main+0x4c06d8 (`orr w8, w25, w0`, w0 = IsHost's answer in the talk handler 0x4c024c) answers yes, so a visitor gets the
//     NPC's conversation;
//   - the host's story keeps going while a session is open (pokopia-native 1.4.0): any session clears the story bit (bit 0) of the mask and
//     the story talks' "not online" conditions fail, so the host gets "Bring <host> here to say hi!" instead. In a story area the host keeps
//     bit 0, and the three talk conditions read IsOnline as false for the host.
// Only with [net] protocol = native (it is in the mods fingerprint after Interact everywhere); turns Interact everywhere on with it; armed by
// the frame callback when main is 2.0.1 and every site is lifted with its expected word.
struct StorySiteFacts {
    bool lifted = false;   // the address is a lifted function of its own (a midhook makes one; the entry hook's site always is one,
                           // so the libs that carry it are told by the midhooks lifted from the same main.sites)
    uint32_t word = 0;     // the instruction word at the site
};
constexpr const char* kStoryBuild = "76a489f3bd3642fa5e18bd83d32c98ba";
constexpr size_t kStorySiteCount = 5;
// The decision of the frame callback with main's facts given (one per kStorySites entry of pkp_linkplay.cpp, in its order; tests): arms Story co-op and sets its mods fingerprint on Ok, else removes its
// hooks. Does nothing (returns Ok) when it was not installed.
InteractCheck story_arm(const std::string& build_id, const StorySiteFacts (&facts)[kStorySiteCount]);

}  // namespace nxrt::games::pkp
