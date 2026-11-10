// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] The floor a client gives an actor the server just placed, which must never take it below the server's height.

#ifndef ZX_NETCLIENT_SERVERFLOOR_COMPUTE_H
#define ZX_NETCLIENT_SERVERFLOOR_COMPUTE_H

#include <cstdint>

namespace zx { namespace netclient {

// [Doruk-Aksoy] The highest floor under the actor's box (`boxFloorz`), unless that is above the height the server put it at (`z`), where it is that height: the floor under its centre, which the client used to fall back to, let gravity drop a monster spanning narrow steps below the server's.
// [Doruk-Aksoy] The console player keeps the box floor, as its prediction needs.
int64_t ComputeServerHeldFloorZ(int64_t boxFloorz, int64_t z, bool consolePlayer);

// [Doruk-Aksoy] Whether the floor must be read again from the map's lines alone: P_CheckPosition stops at the first actor it touches, before it reads a line, leaving the floor under the centre, so a monster touching another sank below the steps under it. The console player keeps its own, as its prediction needs.
bool ComputeRereadFloorFromLines(bool checkPositionGood, bool consolePlayer);

}} // namespace zx::netclient

#endif // ZX_NETCLIENT_SERVERFLOOR_COMPUTE_H
