// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
//
// [MobiusU9B] net-replication's public header for players: what a server sends of one whose state a script changed.

#ifndef ZX_PLAYERSTATE_H
#define ZX_PLAYERSTATE_H

#include "name.h"

class AInventory;

namespace zx { namespace netreplication {

// [MobiusU9B] Send a player's changed user cvar to every client at once, so a client script the server starts later in the tic reads the new value.
void SendUserCVar(int playernum, FName name);

// [rc4l] A player's selected inventory item that a script changed: on a server, sent to every client.
void SendSelectedInventory(int playernum, const AInventory *item, bool showInventoryBar);

}} // namespace zx::netreplication

#endif // ZX_PLAYERSTATE_H
