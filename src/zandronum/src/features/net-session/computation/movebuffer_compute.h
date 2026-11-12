// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How a server drains a client's buffered move commands, and how it counts their arrivals, behind P_Ticker and SERVER_GetPackets.

#ifndef ZX_MOVEBUFFER_COMPUTE_H
#define ZX_MOVEBUFFER_COMPUTE_H

namespace zx { namespace net {

// [rc4l] Ported from zandronum@7feaf8fe410797036f3f6037a74863c51515790a: a second move command runs in a tic only while another waits behind it, or once a second of steady arrivals says the buffer may empty.
bool ComputeRunsSecondMoveCmd(unsigned movesBuffered, unsigned consistentArrivals, unsigned ticRate);

// [rc4l] What a tic says of a client's move commands: nothing yet, one arrived this tic, the last came a tic ago, or more than a tic has passed (zandronum@529ca97fc1be61d4f27e9987b6209c259749ac9c).
enum MoveArrival { MOVE_ARRIVAL_UNKNOWN, MOVE_ARRIVAL_ON_TIME, MOVE_ARRIVAL_LATE, MOVE_ARRIVAL_MISSED };

// [rc4l] The arrival verdict for a client whose last move command came at lastMoveTick, 0 before any since joining.
MoveArrival ComputeMoveArrival(long lastMoveTick, long gametic);

}} // namespace zx::net

#endif // ZX_MOVEBUFFER_COMPUTE_H
