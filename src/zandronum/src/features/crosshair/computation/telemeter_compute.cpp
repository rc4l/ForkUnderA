// [MobiusU9B] See telemeter_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/crosshair/computation/telemeter_compute.h"

#include <cmath>

namespace zx { namespace crosshair {

bool ComputeTelemeterActive(const TelemeterToggles &toggles)
{
	return toggles.on && (toggles.decorations || toggles.monsters || toggles.players || toggles.projectiles);
}

bool ComputeTelemeterMeasures(const TelemeterToggles &toggles, SweepKind kind, bool serverHidesKinds)
{
	if (!ComputeTelemeterActive(toggles)) {
		return false;
	}
	if (serverHidesKinds) {
		return true;
	}
	switch (kind) {
	case kSweepDecoration:
		return toggles.decorations;
	case kSweepMonster:
		return toggles.monsters;
	case kSweepPlayer:
		return toggles.players;
	case kSweepProjectile:
		return toggles.projectiles;
	default:
		return false;
	}
}

int ComputeSweepRange(bool telemeterActive)
{
	return telemeterActive ? kTelemeterRange : kIdentifyRange;
}

std::string ComputeTelemeterText(double distance)
{
	// [MobiusU9B] Written as "not greater" so NaN reads 0, and capped at the range so lround always has a value it can return.
	if (!(distance > 0.0)) {
		return "0 u";
	}
	const double capped = distance < kTelemeterRange ? distance : kTelemeterRange;
	return std::to_string(std::lround(capped)) + " u";
}

} } // namespace zx::crosshair
