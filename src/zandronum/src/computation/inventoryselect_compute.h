// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Selecting a player's inventory-bar item from ACS or the server.

#ifndef ZX_INVENTORYSELECT_COMPUTE_H
#define ZX_INVENTORYSELECT_COMPUTE_H

namespace zx {

// [rc4l] Adapted from zandronum@6107a231590b339460158bf55adcc8f1f92cf806: only an item the player holds that goes on the inventory bar can be selected.
bool ComputeSelectsInventory(bool held, bool onInventoryBar);

// [rc4l] The inventory bar's tics after a selection: raised for five seconds when asked, else left as they were.
int ComputeInventoryBarTics(bool showInventoryBar, int currentTics, int ticRate);

} // namespace zx

#endif
