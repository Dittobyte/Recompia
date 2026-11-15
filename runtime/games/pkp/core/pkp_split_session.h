// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia, split screen: the windows come together on their own (nxrt/split.h, [split] meet). The first window opens a local
// Link Play session once its player is in the world, the others look for it and join. See pkp_split_session.cpp.
#pragma once
#include "core/pkp_feature.h"

namespace nxrt::games::pkp {

extern const Feature kSplitSessionFeature;

}  // namespace nxrt::games::pkp
