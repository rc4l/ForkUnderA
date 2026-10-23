// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A player a server's map command disconnects comes back playing, where every reconnect used to arrive spectating.

#ifndef ZX_REJOIN_H
#define ZX_REJOIN_H

namespace zx { namespace netclient {

// [rc4l] The server said to reconnect for a new map (SVC_MAPNEW): remember whether this client was playing.
void NoteMapNew();

// [rc4l] The connect flags this attempt sends, cl_connect_flags without start-as-spectator while rejoining.
int ConnectFlags(int flags);

// [rc4l] The connection came up or was left, so the next one is the player's own again.
void ForgetRejoin();

}} // namespace zx::netclient

#endif // ZX_REJOIN_H
