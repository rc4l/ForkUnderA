// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/computation/closedline_compute.h"

namespace zx { namespace visibility {

long long ComputeFlatPlaneHeight(bool floor, long long d)
{
	return floor ? -d : d;
}

bool ComputeLineCloses(const ClosedLineIn &in)
{
	if (in.stacked) return false;
	// [rc4l] The back sector's ceiling at or below the front's floor: a lowered ceiling, a shut door seen from below.
	if (in.bsCeiling[0] <= in.fsFloor[0] && in.bsCeiling[1] <= in.fsFloor[1])
		return in.topTexture && !in.bothCeilingsSky;
	// [rc4l] The front's ceiling at or below the back's floor: a raised floor, a lift at the top.
	if (in.fsCeiling[0] <= in.bsFloor[0] && in.fsCeiling[1] <= in.bsFloor[1])
		return in.bottomTexture && !in.bothCeilingsSky;
	// [rc4l] The back sector shut on itself: a closed door, unless an untextured edge keeps the see-through-door effect.
	if (in.bsCeiling[0] <= in.bsFloor[0] && in.bsCeiling[1] <= in.bsFloor[1])
	{
		if ((in.bsCeiling[0] < in.fsCeiling[0] || in.bsCeiling[1] < in.fsCeiling[1]) && !in.topTexture) return false;
		if ((in.bsFloor[0] > in.fsFloor[0] || in.bsFloor[1] > in.fsFloor[1]) && !in.bottomTexture) return false;
		return !in.bothCeilingsSky && !in.bothFloorsSky;
	}
	return false;
}

bool ComputeLineMayClose(const ClosedLineIn &in)
{
	return (in.bsCeiling[0] <= in.fsFloor[0] && in.bsCeiling[1] <= in.fsFloor[1]) ||
		(in.fsCeiling[0] <= in.bsFloor[0] && in.fsCeiling[1] <= in.bsFloor[1]) ||
		(in.bsCeiling[0] <= in.bsFloor[0] && in.bsCeiling[1] <= in.bsFloor[1]);
}

}} // namespace zx::visibility
