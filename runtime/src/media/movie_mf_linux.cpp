// SPDX-License-Identifier: GPL-3.0-or-later
// Linux twin of movie_mf.cpp. Media Foundation has no Linux counterpart here, so the Switch 1 movie:: imports stay with the lifted
// subsdk0 library ([media] movie = off on Windows). Switch 2 titles never use these handlers on either system: their decoders answer on
// the host one level down (src/media/video_codec.cpp).
#include "nxrt/game.h"
#include "nxrt/log.h"
#include "nxrt/movie.h"

namespace nxrt::media {

bool install_movie_hle() {
    if (game::info().platform != game::Platform::Switch2)
        NXLOG_W("movie", "Linux build: movie:: stays in the lifted subsdk0 library (no Media Foundation)");
    return false;
}

MovieStats movie_stats() { return {}; }

}  // namespace nxrt::media
