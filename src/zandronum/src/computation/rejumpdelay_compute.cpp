// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/rejumpdelay_compute.h"

namespace zx {

int ComputeTakeoffJumpTics(int rejumpDelay, bool skulltagJumping, int ticRate)
{
	if (rejumpDelay >= 0)
		return 0;
	return skulltagJumping ? 18 * ticRate / 35 : -1;
}

bool ComputeLandingDelaysJump(int rejumpDelay, bool skulltagJumping, int jumpTics, bool hardLanding)
{
	return skulltagJumping || jumpTics < 0 || hardLanding || rejumpDelay >= 0;
}

int ComputeLandingJumpTics(int rejumpDelay)
{
	return rejumpDelay < 0 ? 7 : rejumpDelay;
}

bool ComputeRisingDelaysJump(bool hasPlayer, int rejumpDelay, bool rising)
{
	return hasPlayer && rising && rejumpDelay >= 0;
}

int ComputeRisingJumpTics(int rejumpDelay)
{
	return rejumpDelay < 2 ? 2 : rejumpDelay;
}

} // namespace zx
