// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia: the waiting screen (pkp_wait_screen.h). Drawn on the overlay's game layer (nxrt/overlay_canvas.h), which has filled
// rectangles and one size of text: the picture is covered in the night blue of the game's own loading screen, a lighter panel in the
// middle holds the two lines, and five squares under them light up one after the other so the player sees the game has not stopped.
// The layer is drawn on the overlay's presenting thread; show and hide come from the game thread.
#include "ui/pkp_wait_screen.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>

#include "nxrt/overlay_canvas.h"

namespace nxrt::games::pkp {

namespace {

constexpr uint32_t kCover = 0x26215cffu, kPanel = 0x3c3489ffu, kTitle = 0xeeedfeffu, kText = 0xcecbf6ffu, kDot = 0x534ab7ffu, kDotLit = 0xeeedfeffu;
constexpr int kDots = 5;
constexpr double kDotSeconds = 0.3;   // how long one square stays lit

struct State {
    std::mutex mu;
    std::string title, text;
    double until = 0;   // steady-clock seconds; 0: not shown
};
State& st() {
    static State s;
    return s;
}

double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

bool overlay_active() {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.mu);
    return s.until > now_s();
}

void overlay_draw(gfx::OverlayCanvas& c) {
    std::string title, text;
    {
        State& s = st();
        std::lock_guard<std::mutex> lk(s.mu);
        title = s.title;
        text = s.text;
    }
    const float w = c.width(), h = c.height(), lh = c.line_height(), pad = lh * 0.9f, dot = lh * 0.5f, gap = dot * 0.6f;
    c.fill(0, 0, w, h, kCover);
    const float dots_w = dot * kDots + gap * (kDots - 1);
    const float inner = std::max({c.text_width(title.c_str()), c.text_width(text.c_str()), dots_w});
    const float pw = inner + pad * 2.f, ph = lh * 2.f + pad * 2.5f + dot;
    const float px = (w - pw) * 0.5f, py = (h - ph) * 0.5f;
    c.fill(px, py, px + pw, py + ph, kPanel);
    c.text((w - c.text_width(title.c_str())) * 0.5f, py + pad, kTitle, title.c_str());
    c.text((w - c.text_width(text.c_str())) * 0.5f, py + pad + lh, kText, text.c_str());
    const int lit = static_cast<int>(now_s() / kDotSeconds) % kDots;
    const float dx = (w - dots_w) * 0.5f, dy = py + pad * 1.5f + lh * 2.f;
    for (int i = 0; i < kDots; ++i) c.fill(dx + (dot + gap) * i, dy, dx + (dot + gap) * i + dot, dy + dot, i == lit ? kDotLit : kDot);
}

void remove() { wait_screen::hide(); }

}  // namespace

namespace wait_screen {

void show(const char* title, const char* text, double seconds) {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.mu);
    s.title = title ? title : "";
    s.text = text ? text : "";
    s.until = seconds > 0 ? now_s() + seconds : 0;
}

void hide() {
    State& s = st();
    std::lock_guard<std::mutex> lk(s.mu);
    s.until = 0;
}

}  // namespace wait_screen

const Feature kWaitScreenFeature = {.name = "wait_screen", .remove = &remove, .overlay_active = &overlay_active, .overlay_draw = &overlay_draw};

}  // namespace nxrt::games::pkp
