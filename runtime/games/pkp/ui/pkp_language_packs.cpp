// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia: language packs. A community pack in the mods folder adds ONE extra language (slot 9) to the game's language screen
// and to the save (issue #590). Recompia ships no translations; the pack supplies the text, the confirmation prompt and the label art.
//
// The game knows nine languages; Spanish is 5. A pack adds language 9 to the first-boot grid and the save, while the game's current-language
// object and every localized-table reader keep using 5. LINKDATA_PATCH records whose real-size field carries 'LATM' in its upper half are held
// here instead of applied over Spain Spanish. Selecting either Spanish swaps those records and lets the game's own language-change callback
// store the choice and reload its tables. Without a pack no marked records are captured and the grid keeps its nine buttons.
//
// Only one extra slot fits: the confirmation-row table (0x283eba8..0x283ebba) is followed at once by the label table, and the label table
// (0x283ebbc..0x283ebce) by another table, so rows for a slot 10 and 11 would overwrite live data. More than one extra language needs the
// two lookups redirected to tables of our own (more hooks); see the pull request.
//
// The record format and language-screen behavior were first documented by Takeitslowww/pokopia-es-latam, whose Latin American Spanish pack
// is the first to use them ('LATM' is that format's marker). The pack supplies the translations, confirmation
// prompts and label artwork through its RomFS overlay; its emulator-specific IPS and subsdk9 files are not used by the recomp.
#include "ui/pkp_language_packs.h"

#include <array>
#include <cstdint>

#include "core/pkp_guest.h"
#include "nxrt/log.h"
#include "nxrt/memory.h"

namespace nxrt::games::pkp {
namespace {

constexpr const char* kTag = "pkp";
constexpr uint32_t kSpanish = 5, kPackLanguage = 9;
constexpr uint64_t kPackMarker = 0x4c41544d;  // 'LATM'
constexpr uint32_t kLinkDataRecords = 12298;
constexpr size_t kMaxCaptured = 1024;

constexpr uint64_t kGridEntries = 0xffa520;
constexpr uint64_t kPatchEntry = 0x11238dc;
// The `cmp w10, w0` after each `ldr w10, [x9]` (x9 = the current-language object, w0 = the requested language).
constexpr std::array kLanguageCompare = {0x230f78ull, 0xffaa3cull};
constexpr uint64_t kLanguageStore = 0x2306e0;
// These stop after the grid's two `ldr w8, [x8]` current-language reads, so the loaded value can be reported as 9.
constexpr std::array kLanguageCurrent = {0xffaca8ull, 0xffb120ull};

constexpr uint64_t kConfirmRows = 0x283ebba;
constexpr uint64_t kLabelRows = 0x283ebbc;
constexpr uint16_t kPackConfirmRow = 0x50, kPackLabelRow = 0x102;

struct RecordEntry {
    uint32_t size = 0;
    uint32_t stored_size = 0;
    uint64_t offset = 0;
    uint16_t flags = 0;
    uint8_t compressed = 0;
    uint8_t from_patch = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(RecordEntry) == 24);

struct Captured {
    uint32_t record = 0;
    uint32_t reserved = 0;
    RecordEntry pack;
    RecordEntry original;
};

struct State {
    uint64_t base = 0;
    uint64_t table = 0;
    std::array<Captured, kMaxCaptured> entries{};
    uint32_t count = 0;
    uint32_t dropped = 0;
    bool active = false;
    bool lookup_rows_checked = false;
    bool lookup_rows_ready = false;
};
State& st() {
    static State s;
    return s;
}

void set_active(bool on) {
    State& s = st();
    if (s.active == on) return;
    if (!guest::ok(s.table, static_cast<uint64_t>(kLinkDataRecords) * sizeof(RecordEntry))) return;
    for (uint32_t i = 0; i < s.count; ++i) {
        const Captured& e = s.entries[i];
        guest::wr<RecordEntry>(s.table + static_cast<uint64_t>(e.record) * sizeof(RecordEntry), on ? e.pack : e.original);
    }
    s.active = on;
    NXLOG_I(kTag, "Language pack %s (%u translated records)", on ? "on" : "off", s.count);
}

// A marked record proves that main is loaded and that a pack is installed. Its two unused language-9 lookup rows live in read-only data.
void prepare_lookup_rows() {
    State& s = st();
    if (s.lookup_rows_checked) return;
    s.lookup_rows_checked = true;
    const uint64_t confirm = s.base + kConfirmRows;
    const uint64_t label = s.base + kLabelRows + 2 * kPackLanguage;
    const uint64_t page = confirm & ~(mem::kPage - 1);
    if (!mem::is_mapped(confirm, 2) || !mem::is_mapped(label, 2)) {
        NXLOG_W(kTag, "Language pack: the language lookup rows are not mapped; no tenth button");
        return;
    }
    const uint16_t old_confirm = guest::rd<uint16_t>(confirm), old_label = guest::rd<uint16_t>(label);
    if ((old_confirm && old_confirm != kPackConfirmRow) || (old_label && old_label != kPackLabelRow)) {
        NXLOG_W(kTag, "Language pack: the language lookup rows are already in use; no tenth button");
        return;
    }
    if (!mem::protect(page, mem::kPage, mem::Prot::RW)) {
        NXLOG_W(kTag, "Language pack: cannot make the language lookup rows writable; no tenth button");
        return;
    }
    guest::wr<uint16_t>(confirm, kPackConfirmRow);
    guest::wr<uint16_t>(label, kPackLabelRow);
    if (!mem::protect(page, mem::kPage, mem::Prot::R)) {
        NXLOG_W(kTag, "Language pack: cannot restore the language lookup rows' protection");
        return;
    }
    s.lookup_rows_ready = true;
}

int hook_patch_entry(::Ctx* c, uint64_t, void*) {
    State& s = st();
    const uint64_t marked_size = c->x[12];
    if ((marked_size >> 32) != kPackMarker) return 0;

    // The original cbz skips a marked entry after it is captured, leaving the game's Spain-Spanish record in place.
    c->x[12] = 0;
    const uint64_t table = c->x[22] + 0x650;
    const uint64_t slot = c->x[9] - 0x13;
    const uint64_t idx = c->x[10] - 0x10;
    if ((s.table && s.table != table) || slot < table || (slot - table) % sizeof(RecordEntry) || !guest::ok(idx, 32)) {
        ++s.dropped;
        return 0;
    }
    const std::array<uint64_t, 4> fields = {
        guest::rd<uint64_t>(idx), guest::rd<uint64_t>(idx + 8), guest::rd<uint64_t>(idx + 16), guest::rd<uint64_t>(idx + 24)};
    uint32_t record = static_cast<uint32_t>(fields[3] >> 32);
    if (!record) record = static_cast<uint32_t>((slot - table) / sizeof(RecordEntry));
    const uint64_t dst = table + static_cast<uint64_t>(record) * sizeof(RecordEntry);
    if (record >= kLinkDataRecords || !guest::ok(dst, sizeof(RecordEntry))) {
        ++s.dropped;
        return 0;
    }

    Captured* captured = nullptr;
    for (uint32_t i = 0; i < s.count; ++i)
        if (s.entries[i].record == record) captured = &s.entries[i];
    if (!captured) {
        if (s.count == kMaxCaptured) {
            ++s.dropped;
            return 0;
        }
        captured = &s.entries[s.count++];
        captured->record = record;
        captured->original = guest::rd<RecordEntry>(dst);
    }
    captured->pack = captured->original;
    captured->pack.size = static_cast<uint32_t>(fields[1]);
    captured->pack.stored_size = static_cast<uint32_t>(fields[2]);
    captured->pack.offset = fields[0];
    captured->pack.compressed = static_cast<uint32_t>(fields[3]) != 0;
    captured->pack.from_patch = 1;
    s.table = table;
    prepare_lookup_rows();
    if (s.active) guest::wr<RecordEntry>(dst, captured->pack);
    return 0;
}

int hook_grid_entries(::Ctx* c, uint64_t, void*) {
    const State& s = st();
    if (!s.count || !s.lookup_rows_ready) return 0;
    const uint64_t holder = c->x[20];
    if (!guest::ok(holder + 8, 8)) return 0;
    const uint64_t list = guest::rd<uint64_t>(holder + 8);
    if (!guest::ok(list, 24)) return 0;
    const uint64_t count = guest::rd<uint64_t>(list + 8), capacity = guest::rd<uint64_t>(list + 16);
    const uint64_t entries = guest::rd<uint64_t>(list);
    if (count >= capacity || !guest::ok(entries, (count + 1) * 12)) return 0;
    for (uint64_t i = 0; i < count; ++i)
        if (guest::rd<uint32_t>(entries + i * 12 + 8) == kPackLanguage) return 0;   // already on this list
    guest::wr<uint32_t>(entries + count * 12, 0x2f);
    guest::wr<uint32_t>(entries + count * 12 + 4, static_cast<uint32_t>(count));
    guest::wr<uint32_t>(entries + count * 12 + 8, kPackLanguage);
    guest::wr<uint64_t>(list + 8, count + 1);
    return 0;
}

// w10 is reloaded by the game's own ldr before the cmp, so changing it here never touches the live language object (read by every
// localized-table reader). A changed value forces the game's store + reload callback even for Spain Spanish <-> the pack, both runtime 5.
int hook_language_compare(::Ctx* c, uint64_t, void*) {
    State& s = st();
    const uint32_t asked = static_cast<uint32_t>(c->x[0]);
    if (asked > kPackLanguage) return 0;
    const bool want = asked == kPackLanguage;
    const uint32_t request = want ? kSpanish : asked;
    c->x[0] = request;
    if (want && !s.count) {
        NXLOG_W(kTag, "The extra language is selected but no language pack is loaded: the game runs in Spain Spanish and the save keeps the choice");
        return 0;
    }
    if (want != s.active) {
        set_active(want);
        c->x[10] = static_cast<uint32_t>(~request);
    }
    return 0;
}

// w0 is dead after the game's store (the table reload takes no argument), so the game itself never sees 9.
int hook_language_store(::Ctx* c, uint64_t, void*) {
    if (st().active && static_cast<uint32_t>(c->x[0]) == kSpanish) c->x[0] = kPackLanguage;
    return 0;
}

int hook_language_current(::Ctx* c, uint64_t, void*) {
    if (st().active && static_cast<uint32_t>(c->x[8]) == kSpanish) c->x[8] = kPackLanguage;
    return 0;
}

void install(const FeatureInstall& in) {
    State& s = st();
    s = State{};
    s.base = in.base;
    in.hooks.add_mid(in.base + kGridEntries, &hook_grid_entries);
    in.hooks.add_mid(in.base + kPatchEntry, &hook_patch_entry);
    for (const uint64_t site : kLanguageCompare) in.hooks.add_mid(in.base + site, &hook_language_compare);
    in.hooks.add_mid(in.base + kLanguageStore, &hook_language_store);
    for (const uint64_t site : kLanguageCurrent) in.hooks.add_mid(in.base + site, &hook_language_current);
}

void remove() {
    State& s = st();
    if (s.active) set_active(false);
    if (s.dropped) NXLOG_W(kTag, "Language pack: %u marked records were invalid or did not fit", s.dropped);
    s = State{};
}

}  // namespace

const Feature kLanguagePacksFeature = {.name = "language_packs", .install = &install, .remove = &remove};

}  // namespace nxrt::games::pkp
