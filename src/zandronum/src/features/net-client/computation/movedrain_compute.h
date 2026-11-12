// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How a client drains the move commands it buffers for each player, behind CLIENT_Tick and AActor::Tick.

#ifndef ZX_MOVEDRAIN_COMPUTE_H
#define ZX_MOVEDRAIN_COMPUTE_H

#include <vector>

namespace zx { namespace netclient {

// [rc4l] Ported from zandronum@93c67c50fda92df93907d9fe1cf67e12857f8e89: a player's run of tics each bringing a move command, broken by a tic that brought none.
unsigned ComputeConsistentArrivals(unsigned arrivals, int lastArrivalTick, int gametic);

// [rc4l] One player as a tic's drain sees them: in the game and playing, how many move commands wait, and their run of steady arrivals.
struct MoveDrainPlayer
{
	bool active;
	unsigned buffered;
	unsigned consistentArrivals;
};

// [rc4l] The players whose oldest command runs, in order: one each first, then a second where the server's own rule (ComputeRunsSecondMoveCmd) allows.
std::vector<int> ComputeMoveDrainOrder(const std::vector<MoveDrainPlayer> &players, unsigned ticRate);

// [rc4l] Whether an actor's tick saves where it was for interpolation, which a client draining its buffers does for players itself.
bool ComputeTickSavesPrevPosition(bool inClientMode, bool isPlayer, bool moveBufferOn);

}} // namespace zx::netclient

#endif // ZX_MOVEDRAIN_COMPUTE_H
