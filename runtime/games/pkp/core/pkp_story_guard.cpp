// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia: Story co-op's safety net, the save copy at start and the story quest journal (pkp_story_guard.h).
//
// The journal's two sites are the setters of the story quest history in the 2.0.1 main (m_story = save root + 0x4a26a0, in the player's
// own save): 0x5d865c (m_story, id, state 1..4) writes a quest's state byte and 0x5d8774 (m_story, id, bool) bit 0 of its flag byte; the
// quest pump 0x11b600 and the event scripts go through them. Both begin with `sub w8, w1, #1`. The entry hooks only read: x0 = m_story,
// w1 = the quest id, w2 = the new value, and the byte in the save is still the old one. The role is the session byte of the game state
// (*(main + 0x3561710) + 0x90: bit 0 online, bit 1 host, bit 2 visitor), the stage its byte at +0x18.
#include "core/pkp_story_guard.h"
#include "core/pkp_guest.h"
#include "core/pkp_save_files.h"
#include "core/pkp_settings.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

#include "nxrt/hooks.h"
#include "nxrt/log.h"
#include "nxrt/memory.h"

namespace nxrt::games::pkp {

namespace {

namespace stdfs = std::filesystem;
constexpr const char* kTag = "pkp";
constexpr const char* kCopyFolder = "story-coop-backup";
constexpr const char* kJournalFolder = "story-journal";
constexpr uintmax_t kJournalMax = 2u << 20;   // past this the journal becomes <key>.1.tsv (one older file is kept)

constexpr uint64_t kGameState = 0x3561710;    // main: -> the game state
constexpr uint64_t kStateStage = 0x18;        // u8: the stage the player is in
constexpr uint64_t kStateSession = 0x90;      // u8: bit 0 online, bit 1 host, bit 2 visitor
constexpr uint32_t kEntryWord = 0x51000428u;  // sub w8, w1, #1: the first instruction of both setters

enum class Change : uintptr_t { State, Flag };
struct Site {
    uint64_t site;
    Change change;
};
constexpr Site kSites[] = {{0x5d865c, Change::State}, {0x5d8774, Change::Flag}};
constexpr size_t kSiteCount = sizeof kSites / sizeof kSites[0];

struct State {
    uint64_t base = 0;
    HookSet* hooks = nullptr;
    uint32_t ids[kSiteCount] = {};         // all 0: off or not installed
    std::atomic<bool> armed{false};
    std::atomic<bool> checked{false};
    std::mutex file_mutex;
    stdfs::path journal;
    bool write_failed = false;             // under file_mutex: logged once
};
State& st() {
    static State s;
    return s;
}

const char* role_str(uint8_t session) {
    if (!(session & 1)) return "offline";
    return (session & 4) ? "visitor" : "host";
}

void append(State& s, const char* role, unsigned stage, uint32_t id, Change change, unsigned before, unsigned after) {
    char when[32];
    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &now);
#else
    gmtime_r(&now, &tmv);
#endif
    std::strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    char line[160];
    const int n = std::snprintf(line, sizeof line, "%s\t%s\t%u\t%u\t%s\t%u\t%u\n", when, role, stage, id,
                                change == Change::State ? "state" : "flag", before, after);
    if (n <= 0) return;

    const std::lock_guard<std::mutex> lock(s.file_mutex);
    std::error_code ec;
    const uintmax_t size = stdfs::file_size(s.journal, ec);
    const bool fresh = static_cast<bool>(ec);
    if (!fresh && size > kJournalMax) {
        stdfs::path older = s.journal;
        older.replace_extension(".1.tsv");
        stdfs::rename(s.journal, older, ec);
    }
    std::ofstream f(s.journal, std::ios::binary | std::ios::app);
    if (f && (fresh || size > kJournalMax)) f << "time\trole\tstage\tquest\tchange\tbefore\tafter\n";
    if (f) f.write(line, n);
    if (!f && !s.write_failed) {
        s.write_failed = true;
        NXLOG_W(kTag, "Story co-op journal: writing %s failed; the story itself is not affected", save_files::u8(s.journal).c_str());
    }
}

int hook_set(::Ctx* c, uint64_t, void* user) {
    State& s = st();
    if (!s.armed.load(std::memory_order_acquire)) return 0;
    const Change change = static_cast<Change>(reinterpret_cast<uintptr_t>(user));
    const uint32_t id = static_cast<uint32_t>(c->x[1]) & 0xffffu;
    const uint64_t off = story_quest_offset(id);
    if (!off) return 0;
    unsigned after = static_cast<unsigned>(c->x[2]) & 0xffu;
    uint64_t at = c->x[0] + off;
    if (change == Change::State) {
        if (after - 1 >= 4) return 0;   // the setter ignores other states
    } else {
        after &= 1;
        ++at;
    }
    if (!guest::ok(at, 1)) return 0;
    unsigned before = guest::rd<uint8_t>(at);
    if (change == Change::Flag) before &= 1;
    if (before == after) return 0;

    uint8_t session = 0, stage = 0;
    const uint64_t game = guest::rd<uint64_t>(s.base + kGameState);
    if (guest::ok(game, kStateSession + 1)) {
        session = guest::rd<uint8_t>(game + kStateSession);
        stage = guest::rd<uint8_t>(game + kStateStage);
    }
    append(s, role_str(session), stage, id, change, before, after);
    return 0;
}

void remove_hooks(State& s) {
    for (uint32_t& h : s.ids) {
        if (h && s.hooks) s.hooks->remove(h);
        h = 0;
    }
}

void install(const FeatureInstall& in) {
    State& s = st();
    if (s.ids[0] || !settings::get_bool(settings::Key::LinkStoryCoop)) return;
    s.base = in.base;
    s.hooks = &in.hooks;
    s.armed.store(false);
    s.checked.store(false);

    static std::atomic<bool> copied{false};   // once per process: the save as it was before the game read it
    if (!copied.exchange(true)) {
        stdfs::path where;
        std::string error;
        if (save_files::copy_account(kCopyFolder, kStoryCopiesKept, &where, &error))
            NXLOG_I(kTag, "Story co-op: the save as it is now was copied to %s (the newest %zu copies are kept)", save_files::u8(where).c_str(),
                    kStoryCopiesKept);
        else if (!error.empty())
            NXLOG_W(kTag, "Story co-op: no copy of the save was made (%s)", error.c_str());
    }

    std::error_code ec;
    const stdfs::path dir = save_files::save_dir() / kJournalFolder;
    stdfs::create_directories(dir, ec);
    s.journal = dir / (save_files::account_key() + ".tsv");
    for (size_t i = 0; i < kSiteCount; ++i)
        s.ids[i] = in.hooks.add_entry(in.base + kSites[i].site, &hook_set, reinterpret_cast<void*>(static_cast<uintptr_t>(kSites[i].change)));
}

// Play mode's frame (main is loaded): the first call checks the two setters' first instruction and arms the hooks or removes them.
void frame(bool, double, play::GameFrame*) {
    State& s = st();
    if (!s.ids[0] || s.checked.exchange(true)) return;
    for (const Site& site : kSites) {
        const uint64_t a = s.base + site.site;
        uint32_t word = 0;
        if (mem::is_mapped(a, 4)) std::memcpy(&word, reinterpret_cast<const void*>(a), 4);
        if (word == kEntryWord) continue;
        remove_hooks(s);
        NXLOG_W(kTag, "Story co-op journal: off: the word at main+0x%llx is %08x, expected %08x (another game version)",
                static_cast<unsigned long long>(site.site), word, kEntryWord);
        return;
    }
    s.armed.store(true, std::memory_order_release);
    NXLOG_I(kTag, "Story co-op journal: every story quest change is written to %s", save_files::u8(s.journal).c_str());
}

void remove() {   // the registry has removed the hooks
    State& s = st();
    s.armed.store(false);
    s.checked.store(false);
    for (uint32_t& h : s.ids) h = 0;
    s.hooks = nullptr;
}

}  // namespace

uint64_t story_quest_offset(uint32_t id) {
    if (id - 1 >= 1000) return 0;
    return id > 800 ? 0x1860 + 2ull * (id - 800) : 0x1060 + 2ull * id;
}

const Feature kStoryGuardFeature = {.name = "story_guard", .install = &install, .remove = &remove, .frame = &frame};

}  // namespace nxrt::games::pkp
