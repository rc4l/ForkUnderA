// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a player's start item did to the player APlayerPawn::GiveDefaultInventory gave it to.

#ifndef ZX_STARTITEM_COMPUTE_H
#define ZX_STARTITEM_COMPUTE_H

namespace zx {

// [rc4l] Ported from zandronum@832625b0700b5d923f1381bc1e790f1d2260e223: whether a start item morphed its player, which only an item it picked up can, so a refused one never reads who took it.
bool ComputeStartItemMorphedPlayer(bool pickedUp, bool takenByThePlayer);

} // namespace zx

#endif
