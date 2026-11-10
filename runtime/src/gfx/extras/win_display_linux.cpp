// SPDX-License-Identifier: GPL-3.0-or-later
// Linux twin of win_display.cpp. Linux has no system-wide "HDR is on for this monitor" answer the runtime can read: on gamescope (the
// Steam Deck / Bazzite game mode) and on Wayland compositors with colour management, the Vulkan surface lists HDR formats only when the
// compositor can show them. The query answers "unknown" (valid = false), so [hdr] mode = auto keeps SDR and [hdr] mode = on picks the
// HDR format the surface offers.
#include <cstdio>

#include "extras/win_display.h"

namespace nxrt::extras {

bool query_display_hdr(void*, DisplayHdrInfo* out) {
    if (!out) return false;
    *out = DisplayHdrInfo{};
    std::snprintf(out->monitor, sizeof out->monitor, "%s", "(Linux)");
    std::snprintf(out->how, sizeof out->how, "%s", "no display HDR query on Linux: set [hdr] mode = on for HDR");
    return false;
}

}  // namespace nxrt::extras
