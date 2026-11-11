// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia: the effective-mods notice. The gameplay mods are the table's rows marked as gameplay mods (read from the table, so a future
// extra shows up here by itself); menu_in_game and taller_fov are the Testing tab's two experimental switches. Values come only from
// settings::get_bool, so an env override or the player's own ini value is reported as it is. Nothing is written.
#include "core/pkp_mods_notice.h"

#include <cstring>

#include "core/pkp_settings.h"
#include "nxrt/log.h"

namespace nxrt::games::pkp {
namespace mods_notice {
namespace {

constexpr const char* kTag = "pkp";
using settings::Key;

struct Label { Key key; const char* label; };
// Player-facing names (the launcher's GAMEPLAY / EXPERIMENTAL_ROWS wording). A gameplay key without one is shown by its key name.
constexpr Label kLabels[] = {
    {Key::JumpMomentum, "Running jump"},
    {Key::CameraAim, "Camera aim"},
    {Key::InventorySort, "Sort"},
    {Key::CraftSearch, "Search"},
    {Key::TitleQuit, "Quit Game"},
    {Key::MenuInGame, "Mouse in the game's menus"},
    {Key::TallerFov, "Taller view on 16:10 screens"},
    {Key::Fix4x3, "4:3 Fix"},
};

const char* label_of(const settings::Spec& sp) {
    for (const Label& l : kLabels)
        if (l.key == sp.key) return l.label;
    return sp.name;
}

void add(std::vector<Entry>& out, Key k, Kind kind) {
    const settings::Spec& sp = settings::spec(k);
    out.push_back({sp.name, label_of(sp), kind, settings::get_bool(k)});
}

void install(const FeatureInstall&) {
    const std::vector<Entry> e = snapshot();
    auto keys = [&](Kind kind) {
        std::string s;
        for (const Entry& x : e)
            if (x.kind == kind && x.on) s += (s.empty() ? "" : ", ") + std::string(x.key);
        return s.empty() ? std::string("none") : s;
    };
    NXLOG_I(kTag, "mods: gameplay on: %s; experimental on: %s", keys(Kind::Gameplay).c_str(), keys(Kind::Experimental).c_str());
}

}  // namespace

std::vector<Entry> snapshot() {
    std::vector<Entry> out;
    for (int i = 0; i < static_cast<int>(Key::Count); ++i) {
        const Key k = static_cast<Key>(i);
        const settings::Spec& sp = settings::spec(k);
        if (sp.gameplay && sp.parent == Key::Count) add(out, k, Kind::Gameplay);   // the mods' switches, not their settings (camera_aim_reach)
    }
    add(out, Key::MenuInGame, Kind::Experimental);
    add(out, Key::TallerFov, Kind::Experimental);
    add(out, Key::Fix4x3, Kind::Experimental);
    return out;
}

std::string text(const std::vector<Entry>& entries, Kind kind) {
    std::string list;
    for (const Entry& e : entries)
        if (e.kind == kind && e.on) list += (list.empty() ? "" : ", ") + std::string(e.label);
    if (list.empty()) list = "none";
    return (kind == Kind::Gameplay ? "Enabled gameplay mods: " : "Experimental features enabled: ") + list + ".";
}

}  // namespace mods_notice

const Feature kModsNoticeFeature = {.name = "mods_notice", .install = &mods_notice::install};

}  // namespace nxrt::games::pkp
