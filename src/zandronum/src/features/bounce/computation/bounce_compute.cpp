// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/bounce/computation/bounce_compute.h"

#include <cmath>

namespace zx { namespace bounce {

bool ComputeBounceSpent(int &bouncecount)
{
	return bouncecount > 0 && --bouncecount == 0;
}

bool ComputeTriesWallBounce(int bounceFlags)
{
	return (bounceFlags & (ZX_BOUNCE_Walls | ZX_BOUNCE_Floors | ZX_BOUNCE_Ceilings)) != 0;
}

bool ComputeBouncesOffPlane(int bounceFlags, bool ceiling)
{
	return (bounceFlags & (ceiling ? ZX_BOUNCE_Ceilings : ZX_BOUNCE_Floors)) != 0;
}

OnTop ComputeOnTopResult(bool bounced, bool isMissile)
{
	if (bounced)
		return OnTop::Bounced;
	return isMissile ? OnTop::Explode : OnTop::Land;
}

double ComputeHeadOn(double lineDx, double lineDy, double velX, double velY)
{
	const double length = std::sqrt(lineDx * lineDx + lineDy * lineDy);
	return length > 0 ? std::fabs(lineDx * velY - lineDy * velX) / length : 0;
}

bool ComputeKeepsRefusedLine(bool haveOne, double keptHeadOn, double headOn)
{
	return !haveOne || headOn > keptHeadOn;
}

bool ComputeClientLeavesActorsToServer(bool serverOwned, bool isMissile, int bounceFlags)
{
	return serverOwned && isMissile && bounceFlags != 0;
}

}}
