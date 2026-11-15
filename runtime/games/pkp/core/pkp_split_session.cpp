// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia, split screen: the windows come together on their own ([split] meet, nxrt/split.h). The game has one Link Play
// session manager (pkp_layout.h kSessionOwner), made at start-up and worked on every frame, and its multiplayer menu only makes
// requests of it: open a session, look for sessions, join one. This module makes the same three requests the menu makes for "Local
// connection" without a password, so no menu is opened and nothing is pressed for the player:
//   window 1         once its player is in the world: kSessionCreate. The manager saves, opens the session and sets the session bits
//                    (online + host) about 5 s later.
//   windows 2 to 4   once their player is in the world: kSessionSearch, and when the manager is idle again and found a session,
//                    kSessionJoin(0). The manager connects and starts the trip to the host's world itself; the session bits (online +
//                    visitor) are set when the player has arrived. A search that finds nothing (window 1 is not there yet) is made
//                    again a little later.
// The game puts a visitor at its arrival point, the PC. The first player may be anywhere (it opens the session from where it stands,
// in the first cave of a new game even, which cannot be reached from the PC), so with [split] together a visitor that arrived is put
// where the first player stands, once it walks: by the game's own warp of a player (kPlayerWarp, what its script command for moving a
// player calls), with the first player's position (kPosition of the player manager's other character). Both games then show the two
// side by side. On the very spot, not beside it: the game moves two characters apart itself, and a spot beside may be in rock. The
// game goes on placing a visitor after the session bits say it arrived (a new character about 4 s later, put at the PC about 8 s
// later), which undoes a warp made before; it reads the player's input again about 13 s later, and that is when the warp is made. A
// visitor found far from the first player a moment after a warp is warped again.
// A window that joins shows the waiting screen (pkp_wait_screen.h) from its first search until it is there: while the first window is
// not in the world yet, through the trip, and until it stands with the first player. So the player does not play on in a world of
// their own that they are about to leave, and does not see the stop at the PC.
// The windows reach each other through the runtime's Link Play on this PC only (the starter gives the group a port of its own), so the
// one session a search can find is window 1's. The game unlocks its multiplayer menu at Environment Level 2; the requests themselves
// do not ask for it, so a group can meet before that.
// "In the world" is: play mode sees the game read the player's input (the frame callback's in_game), the game is not in an event and
// the autosave is not writing. A request is made on the game thread, as the menu makes it. What the player does afterwards is theirs: a
// player who leaves the session is not brought back.
#include "core/pkp_split_session.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>

#include "core/pkp_guest.h"
#include "core/pkp_layout.h"
#include "nxrt/dispatch.h"
#include "nxrt/log.h"
#include "nxrt/notices.h"
#include "nxrt/split.h"
#include "ui/pkp_wait_screen.h"

namespace nxrt::games::pkp {

namespace {

constexpr const char* kTag = "pkp";
constexpr const char* kNotice = "pkp.split";

constexpr double kSettle = 2.0;        // the player has been in the world this long before the first request
constexpr double kAgain = 3.0;         // between a search that found nothing and the next one, and after a refused request
constexpr double kOpenWait = 30.0;     // the session bits are expected this long after the manager took the request to open
constexpr double kJoinWait = 90.0;     // and this long after the request to join (the trip loads the host's world)
constexpr double kCover = 600.0;       // the waiting screen's own end, renewed at every step: it never outlives this module's work by more
constexpr double kPlaceWait = 60.0;    // the visitor is put with the first player within this time of arriving, or stays at the PC
constexpr double kLanded = 1.2;        // after a placing: when the visitor's place is looked at again
constexpr float kNear = 400.0f;        // the visitor is with the first player within this distance (world units; a character is about 60 wide)
constexpr int kPlacings = 3;           // placings before the visitor is left where the game has it
constexpr size_t kVec4 = 4 * sizeof(float);
constexpr int kMaxTries = 5;           // requests to open, or joins, that came to nothing before the module stops trying

enum class Step { Off, Wait, Opening, Searching, Joining, Placing, Landing, Done };

struct State {
    uint64_t base = 0;
    int window = 0;
    Step step = Step::Off;
    std::atomic<bool> in_game{false};   // play mode's frame (main thread) -> the game thread
    double since = -1;                  // Wait: when the player was first seen in the world; a request: when it was made
    double not_before = 0;              // Wait: no request before this time
    int tries = 0;
    int placings = 0;
    bool told_waiting = false;
    bool checked = false;               // the requests exist in this main
};
State& st() {
    static State s;
    return s;
}

using guest::rd;

double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

uint64_t manager(uint64_t base) {
    if (!guest::ok(base + layout::kSessionOwner, 8)) return 0;
    const uint64_t owner = rd<uint64_t>(base + layout::kSessionOwner);
    if (!guest::ok(owner + layout::kOwnerSession, 8)) return 0;
    const uint64_t mgr = rd<uint64_t>(owner + layout::kOwnerSession);
    return guest::ok(mgr, layout::kSessionSize) ? mgr : 0;
}

uint8_t session_bits(uint64_t game) { return rd<uint8_t>(game + layout::kStateSession); }

// The player walks around: not in an event, and the autosave is not writing (the manager's own wait before it saves).
bool player_free(const State& s, uint64_t game) {
    if (!s.in_game.load(std::memory_order_acquire)) return false;
    if (rd<uint32_t>(game + layout::kStateAutosave) - 1 < 4) return false;
    const uint64_t event = rd<uint64_t>(game + layout::kStateEvent);
    return !(event && guest::ok(event, layout::kEventBusy + 1) && rd<uint8_t>(event + layout::kEventBusy));
}

// The visitor's own character and the first player's, when both can be read; false otherwise.
bool characters(const State& s, uint64_t* me, uint64_t* first) {
    const uint64_t players = rd<uint64_t>(s.base + layout::kPlayerManager);
    if (!guest::ok(players, layout::kManagerOther + 8)) return false;
    *me = rd<uint64_t>(players + layout::kManagerPlayer);
    *first = rd<uint64_t>(players + layout::kManagerOther);
    return guest::ok(*me, layout::kPlayerModeByte + 1) && guest::ok(*first + layout::kPosition, kVec4);
}

// A character's position (vec4: x, y, z and the game's fourth float); false while it has none (a character just made is at 0, 0, 0).
bool position(uint64_t character, float at[4]) {
    std::memcpy(at, reinterpret_cast<const void*>(character + layout::kPosition), kVec4);
    return std::isfinite(at[0]) && std::isfinite(at[1]) && std::isfinite(at[2]) && !(at[0] == 0 && at[1] == 0 && at[2] == 0);
}

// kPlayerWarp(character, &position, 0, 1, 1, 1, 0, 0, stack: u8 0), the arguments the game's script command gives it. The position and
// the stack argument (the callee's [sp]) are put below the game thread's stack pointer for the call.
void warp(::Ctx* c, uint64_t base, uint64_t character, const float at[4]) {
    const uint64_t sp = (c->x[31] - 0x100) & ~0xfull, pos = sp + 0x10;
    std::memcpy(reinterpret_cast<void*>(pos), at, kVec4);
    guest::wr<uint64_t>(sp, 0);
    guest::call_n<8>(c, base + layout::kPlayerWarp, std::array<uint64_t, 8>{character, pos, 0, 1, 1, 1, 0, 0}, sp);
}

void placed(State& s) {
    s.step = Step::Done;
    wait_screen::hide();
}

// Windows 2 to 4, arrived: to the first player's spot, once the player walks.
void place(State& s, ::Ctx* c, uint64_t game, double now) {
    if (!(session_bits(game) & layout::kSessionVisitor) || now - s.since > kPlaceWait) return placed(s);
    if (!player_free(s, game) || now - s.since < 1.0) return;
    uint64_t me, first;
    float mine[4], at[4];
    if (!characters(s, &me, &first) || !position(me, mine) || !position(first, at)) return;
    if (rd<uint32_t>(me + layout::kPlayerMode) != 0 || rd<uint8_t>(me + layout::kPlayerModeByte) != 0) return;   // not on foot
    warp(c, s.base, me, at);
    ++s.placings;
    s.step = Step::Landing;
    s.since = now;
    NXLOG_I(kTag, "split screen: window %d is put where the first player stands (%.0f, %.0f, %.0f)", s.window, at[0], at[1], at[2]);
}

// A moment after a placing: with the first player, or sent back by the game (then once more, a few times at most).
void land(State& s, uint64_t game, double now) {
    if (now - s.since < kLanded) return;
    uint64_t me, first;
    float mine[4], theirs[4];
    const bool known = (session_bits(game) & layout::kSessionVisitor) && characters(s, &me, &first) && position(me, mine) && position(first, theirs);
    const bool near = known && std::hypot(mine[0] - theirs[0], mine[1] - theirs[1], mine[2] - theirs[2]) <= kNear;
    if (near || !known || s.placings >= kPlacings) {
        if (known && !near) NXLOG_W(kTag, "split screen: window %d stays where the game has it, the first player's spot did not hold", s.window);
        return placed(s);
    }
    s.step = Step::Placing;
    s.since = now;
}

void stop(State& s, const char* why) {
    s.step = Step::Done;
    wait_screen::hide();
    notices::post(kNotice, "Split screen: could not bring the players together. Use Link Play at the PC.", 10.0);
    NXLOG_W(kTag, "split screen: %s; the players can still meet through the game's Link Play menu", why);
}

// Back to waiting for the next request; false when the module has given up.
bool again(State& s, double now, const char* why) {
    if (++s.tries >= kMaxTries) {
        stop(s, why);
        return false;
    }
    NXLOG_I(kTag, "split screen: %s, trying again (%d of %d)", why, s.tries + 1, kMaxTries);
    s.step = Step::Wait;
    s.not_before = now + kAgain;
    return true;
}

void wait(State& s, ::Ctx* c, uint64_t mgr, uint64_t game, double now) {
    if (session_bits(game) & layout::kSessionOnline) return placed(s);   // the player got into a session another way (the menu)
    if (!player_free(s, game) || rd<uint32_t>(mgr + layout::kSessionState) != 0) {
        s.since = -1;
        return;
    }
    if (s.since < 0) s.since = now;
    if (now - s.since < kSettle || now < s.not_before) return;
    const bool first = s.window == 1;
    const uint64_t took = guest::call(c, s.base + (first ? layout::kSessionCreate : layout::kSessionSearch), mgr, 0) & 1;
    if (!took) {
        s.not_before = now + kAgain;
        return;
    }
    s.step = first ? Step::Opening : Step::Searching;
    s.since = now;
    if (first) {
        notices::post(kNotice, "Split screen: opening the session...", kOpenWait);
        NXLOG_I(kTag, "split screen: window 1 asked the game to open a local session");
    } else if (!s.told_waiting) {
        wait_screen::show("Joining player 1", "Looking for their game...", kCover);
    }
}

// The requests are lifted functions of this main (asked once the game runs: main is not mapped yet at install).
bool requests_exist(State& s) {
    if (s.checked) return true;
    for (const uint64_t fn : {layout::kSessionCreate, layout::kSessionSearch, layout::kSessionJoin, layout::kPlayerWarp})
        if (!fn_lookup(s.base + fn)) {
            s.step = Step::Off;
            NXLOG_W(kTag, "split screen: the game function at main+%#llx is not in this build; the players meet through the game's Link Play menu",
                    static_cast<unsigned long long>(fn));
            return false;
        }
    s.checked = true;
    return true;
}

void game_frame(::Ctx* c) {
    State& s = st();
    if (s.step == Step::Off || s.step == Step::Done || !requests_exist(s)) return;
    const uint64_t mgr = manager(s.base);
    const uint64_t game = rd<uint64_t>(s.base + layout::kGameState);
    if (!mgr || !guest::ok(game, layout::kStateAutosave + 4)) return;
    const double now = now_s();
    const uint8_t bits = session_bits(game);
    const bool idle = rd<uint32_t>(mgr + layout::kSessionState) == 0;
    switch (s.step) {
        case Step::Wait:
            wait(s, c, mgr, game, now);
            break;
        case Step::Opening:
            if (bits & layout::kSessionHost) {
                s.step = Step::Done;
                notices::post(kNotice, "Split screen: the session is open", 4.0);
                NXLOG_I(kTag, "split screen: window 1's session is open");
            } else if (idle && now - s.since > kOpenWait) {
                again(s, now, "the session did not open");
            }
            break;
        case Step::Searching: {
            if (!idle) break;
            if (rd<uint32_t>(mgr + layout::kSessionFound) == 0) {   // window 1 is not there yet: no count, it may take its player a while
                if (!s.told_waiting) wait_screen::show("Waiting for player 1", "Their game is not in the world yet...", kCover);
                s.told_waiting = true;
                s.step = Step::Wait;
                s.not_before = now + kAgain;
                break;
            }
            if (guest::call(c, s.base + layout::kSessionJoin, 0) & 1) {
                s.step = Step::Joining;
                s.since = now;
                wait_screen::show("Joining player 1", "Loading their world...", kCover);
                NXLOG_I(kTag, "split screen: window %d asked the game to join window 1's session", s.window);
            } else {
                again(s, now, "the game refused the join");
            }
            break;
        }
        case Step::Joining:
            if (bits & layout::kSessionVisitor) {
                NXLOG_I(kTag, "split screen: window %d is in window 1's world", s.window);
                if (split::together()) {
                    s.step = Step::Placing;
                    s.since = now;
                    wait_screen::show("Almost there", "Taking you to player 1...", kPlaceWait + kLanded * kPlacings);
                } else {
                    placed(s);
                }
            } else if (idle && now - s.since > kJoinWait) {
                again(s, now, "the join did not arrive");
            }
            break;
        case Step::Placing:
            place(s, c, game, now);
            break;
        case Step::Landing:
            land(s, game, now);
            break;
        case Step::Off:
        case Step::Done:
            break;
    }
}

void frame(bool in_game, double, play::GameFrame*) { st().in_game.store(in_game, std::memory_order_release); }

void install(const FeatureInstall& in) {
    State& s = st();
    if (!split::meet()) return;
    s.base = in.base;
    s.window = split::window();
    s.step = Step::Wait;
    NXLOG_I(kTag, "split screen: window %d %s on its own once its player is in the world", s.window,
            s.window == 1 ? "opens a local session" : "joins window 1's session");
}

void remove() {
    State& s = st();
    s.base = 0;
    s.window = 0;
    s.step = Step::Off;
    s.in_game.store(false, std::memory_order_release);
    s.since = -1;
    s.not_before = 0;
    s.tries = 0;
    s.placings = 0;
    s.told_waiting = false;
    s.checked = false;
}

}  // namespace

const Feature kSplitSessionFeature = {.name = "split_session", .install = &install, .remove = &remove, .frame = &frame, .game_frame = &game_frame};

}  // namespace nxrt::games::pkp
