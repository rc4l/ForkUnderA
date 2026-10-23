// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] net-session's public header: what role this process plays in a game, asked here rather than of NETWORK_GetState so one module decides what each role means.

#ifndef ZX_NETROLE_H
#define ZX_NETROLE_H

#include "network.h"

namespace zx { namespace net {

// [rc4l] Whether this process hosts the game for others.
inline bool IsServer() { return NETWORK_GetState() == NETSTATE_SERVER; }

// [rc4l] Whether this process plays on another's server.
inline bool IsClient() { return NETWORK_GetState() == NETSTATE_CLIENT; }

// [rc4l] Whether this is a game for one player with no server or client involved.
inline bool IsSinglePlayer() { return NETWORK_GetState() == NETSTATE_SINGLE; }

// [rc4l] Whether this is a game for several players on one machine, with bots and no network.
inline bool IsLocalMultiplayer() { return NETWORK_GetState() == NETSTATE_SINGLE_MULTIPLAYER; }

// [rc4l] Whether this process only applies what a server decides: a client, or a client's demo playing back.
inline bool InClientMode() { return NETWORK_InClientMode(); }

}} // namespace zx::net

#endif // ZX_NETROLE_H
