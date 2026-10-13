// [MobiusU9B] The telemeter's rules: when it runs, what it measures and how the distance reads, kept engine-free so they are unit-tested.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_TELEMETER_COMPUTE_H
#define ZX_TELEMETER_COMPUTE_H

#include "features/crosshair/computation/viewsweep_compute.h"

#include <string>

namespace zx { namespace crosshair {

// [MobiusU9B] The telemeter's switch and its per-kind toggles as currently set.
struct TelemeterToggles
{
	bool on;
	bool decorations;
	bool monsters;
	bool players;
	bool projectiles;
};

// [MobiusU9B] Whether the telemeter has anything to do: switched on with at least one kind to measure.
bool ComputeTelemeterActive(const TelemeterToggles &toggles);

// [MobiusU9B] Whether the telemeter measures an actor of kind; serverHidesKinds (sv_noidentifytarget) measures every kind so the toggles can't tell actors apart.
bool ComputeTelemeterMeasures(const TelemeterToggles &toggles, SweepKind kind, bool serverHidesKinds);

// [MobiusU9B] How far the sweep must reach in map units: the telemeter's range while it is active, else the actor name's.
int ComputeSweepRange(bool telemeterActive);

// [MobiusU9B] The gap from where the view line leaves the viewer's body (viewerExit) to where it enters the target's, so touching reads 0; a mod-widened attack box (targetBox) stands in when the line misses the body.
double ComputeTelemeterGap(const SweepEntry &targetBody, double targetBox, double viewerExit);

// [MobiusU9B] The telemeter's line for a distance in map units, rounded, as "384 u"; nothing sensible reads 0.
std::string ComputeTelemeterText(double distance);

} } // namespace zx::crosshair

#endif
