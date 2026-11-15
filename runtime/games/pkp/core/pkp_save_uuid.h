// SPDX-License-Identifier: GPL-3.0-or-later
// Pokémon Pokopia: a save's player UUID made its own (pkp_save_uuid.cpp): the repair of the shared one ([pkp] uuid_repair), and split
// screen's visiting players.
// Every save is given a player UUID at new game (main 0x5aef60, nn::util::GenerateUuid at 0x5af08c), and Link Play's host refuses a
// visitor with its own UUID (0x5463c0, error 9005). Until GetInfo RandomEntropy came from the OS (src/kernel/svc_info.cpp) it was one
// fixed value, so every save made with this runtime got the same UUID, b9ae85ed-20e7-4f6b-89a1-9f30fcf9f0f9, and no two players could
// visit each other. At start, before the game reads its save, a save with that UUID gets a fresh random one in every place it is stored:
// the owner stamps in CMNDAT (G00 and its backup B00; zlib from 0x130, end offset at +0x10) and the plain copy in S00/SYDAT. STGDAT has
// none. A copy of the account folder is kept first, in <save dir>/uuid-backup/<time>/.
// Split screen (nxrt/split.h): a visiting window's save can be a copy of the first player's, with the first player's UUID, which the
// first window's game would refuse the same way. In windows 2 to 4 a save with the UUID of the first player's save ([split]
// first_account) gets a new one in the same places, once: after that they differ.
#pragma once

#include "core/pkp_feature.h"

namespace nxrt::games::pkp {

// Runs once per process; does nothing when [pkp] uuid_repair = false, without a save, or when the save has another UUID. Logged.
void save_uuid_repair();
// The registry's first feature: its install runs the repair and the visiting player's check, before the game is loaded (so before it
// reads its save).
extern const Feature kSaveUuidFeature;

}  // namespace nxrt::games::pkp
