// SPDX-License-Identifier: GPL-3.0-or-later
// macOS: no host backend for Nintendo's `movie::` library yet. The Windows one (movie_mf.cpp) demuxes and decodes through
// Media Foundation; the macOS counterpart is AVFoundation / VideoToolbox and is milestone M6 in docs/macos-port.md (D10).
//
// Until it lands this file answers the two questions the core asks, honestly: nothing is installed, so the game's own lifted
// movie:: library in subsdk0 runs (the same thing [media] movie = off does on Windows), and the statistics stay zero. A
// movie that the game plays will therefore fail where the lifted library needs NVDEC/VIC, not here.
#include "nxrt/log.h"
#include "nxrt/movie.h"

namespace nxrt::media {

bool install_movie_hle() {
    static bool logged = false;
    if (!logged) {
        logged = true;
        NXLOG_W("media", "no host movie backend on macOS yet (docs/macos-port.md M6, AVFoundation): the game's own lifted movie:: library runs");
    }
    return false;
}

MovieStats movie_stats() { return {}; }

}  // namespace nxrt::media
