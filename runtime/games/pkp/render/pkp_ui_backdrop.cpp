// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia: full-screen UI backdrops on pictures narrower than 16:9 (4:3, 16:10, 3:2).
//
// On a narrower picture the 3D view is taller (pkp_aspect.cpp) but the UI keeps its 1920x1080 canvas, centred, so every pane authored to fill
// the canvas (a backdrop, a dim or frame filter, a fade square, the title's photo) ends at the 16:9 strip and the taller scene shows above
// and below it. A pane that exactly fills the canvas (rectangle 0,0..1920,1080 from its world matrix, size and anchor) is scaled
// by the ratio of the taller canvas to 1080 in both directions as it draws, so it covers the whole picture without changing its proportions (the
// extra width falls outside the screen). Menus, text and buttons are not canvas-sized and stay where they are. 16:9 and wider pictures
// install nothing. The panes are seen through the menu module's draw hooks (pkp_menu.h: it owns those addresses).
#include "render/pkp_ui_backdrop.h"

#include <atomic>
#include <cmath>
#include <cstdint>

#include "input/pkp_play_internal.h"
#include "nxrt/log.h"
#include "nxrt/memory.h"
#include "ui/pkp_menu.h"

namespace nxrt::games::pkp {
namespace {

constexpr const char* kTag = "pkp";
constexpr float kCanvasW = 1920.f, kCanvasH = 1080.f, kEdge = 1.0f;   // kEdge: canvas pixels of slack when matching the canvas

float g_scale = 1.0f;                       // taller canvas / 1080, set at install, read by the drawing thread
std::atomic<uint32_t> g_scaled{0};          // panes scaled (status and one log line)

void on_pane_draw(uint64_t pane) {
    float x0, y0, x1, y1;
    if (!menu_pane_rect(pane, &x0, &y0, &x1, &y1)) return;
    if (std::fabs(x0) > kEdge || std::fabs(y0) > kEdge || std::fabs(x1 - kCanvasW) > kEdge || std::fabs(y1 - kCanvasH) > kEdge) return;   // not the canvas
    menu_pane_scale(pane, g_scale);   // now larger than the canvas: it does not match again
    if (g_scaled.fetch_add(1, std::memory_order_relaxed) == 0) NXLOG_I(kTag, "ui backdrop: first canvas-sized pane scaled by %.3f", static_cast<double>(g_scale));
}

void install(const FeatureInstall&) {
    using namespace play_detail;
    const Wide& w = wide();
    if (!(w.aspect < kAspect169) || close_to(w.aspect, kAspect169) || !w.ui_size || !mem::is_mapped(w.ui_size, 8)) return;
    const int32_t ui_h = rd<int32_t>(w.ui_size + 4);   // the layout camera's canvas height (pkp_play.cpp: 1920 wide, 1920 / aspect high)
    if (ui_h <= 1080) return;
    g_scale = static_cast<float>(ui_h) / kCanvasH;
    g_scaled.store(0, std::memory_order_relaxed);
    menu_add_pane_draw_client(&on_pane_draw);
    NXLOG_I(kTag, "ui backdrop: aspect %.4f, canvas-sized panes are scaled by %.3f to fill the picture", w.aspect, static_cast<double>(g_scale));
}

void remove() { menu_remove_pane_draw_client(&on_pane_draw); }

}  // namespace

const Feature kUiBackdropFeature = {.name = "ui_backdrop", .install = &install, .remove = &remove};

}  // namespace nxrt::games::pkp
