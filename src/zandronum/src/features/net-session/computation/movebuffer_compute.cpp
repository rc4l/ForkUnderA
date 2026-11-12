// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-session/computation/movebuffer_compute.h"

namespace zx { namespace net {

bool ComputeRunsSecondMoveCmd(unsigned movesBuffered, unsigned consistentArrivals, unsigned ticRate)
{
	return movesBuffered != 1 || consistentArrivals >= ticRate;
}

MoveArrival ComputeMoveArrival(long lastMoveTick, long gametic)
{
	if (lastMoveTick == 0)
		return MOVE_ARRIVAL_UNKNOWN;
	return lastMoveTick == gametic ? MOVE_ARRIVAL_ON_TIME : MOVE_ARRIVAL_MISSED;
}

}} // namespace zx::net
