// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Whether a client a server's map command sends back to reconnect asks to play again, and the connect flags that ask it.

#ifndef ZX_REJOIN_COMPUTE_H
#define ZX_REJOIN_COMPUTE_H

namespace zx { namespace netclient {

// [rc4l] A client in the game and not spectating when the map command came rejoins as a player.
bool ComputeRejoinsAfterMapNew(bool inGame, bool spectating);

// [rc4l] The connect flags an attempt sends: a rejoining client's without the start-as-spectator bit, any other's as set.
int ComputeConnectFlags(int flags, int startAsSpectatorBit, bool rejoin);

}} // namespace zx::netclient

#endif
