// [rc4l] Implementation of the fov-interp tween arithmetic. See the header for provenance.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fovinterp_compute.h"

#include <cmath>

namespace zx
{

// R_SetFOV's accepted range. Duplicated as plain constants rather than pulled from the engine —
// this TU must stay header-pure so the tests can link it on its own.
static const float FOV_MIN = 5.0f;
static const float FOV_MAX = 170.0f;
// Fraction of the remaining distance used when it exceeds the flat step. ZDoom's original.
static const float FOV_PROPORTIONAL = 0.025f;
static const float FOV_SPEED_MIN = 1.0f;

float FovChangeSpeedClamp(float changeSpeed)
{
	return changeSpeed < FOV_SPEED_MIN ? FOV_SPEED_MIN : changeSpeed;
}

float FovClamp(float fov)
{
	if (fov < FOV_MIN)
		return FOV_MIN;
	if (fov > FOV_MAX)
		return FOV_MAX;
	return fov;
}

float FovTargetForWeapon(float desiredFov, bool alive, bool hasReadyWeapon, float weaponFovScale)
{
	// A zero scale means "this weapon does not adjust the FOV" — it must not zero the target.
	if (!alive || !hasReadyWeapon || weaponFovScale == 0.0f)
		return desiredFov;

	return desiredFov * fabsf(weaponFovScale);
}

float FovStepTic(float currentFov, float targetFov, float changeSpeed)
{
	if (currentFov == targetFov)
		return targetFov;

	const float speed = FovChangeSpeedClamp(changeSpeed);
	const float distance = fabsf(currentFov - targetFov);

	// Close enough to finish in one tic — land exactly on the target instead of overshooting.
	if (distance < speed)
		return targetFov;

	const float proportional = distance * FOV_PROPORTIONAL;
	const float step = proportional > speed ? proportional : speed;

	return currentFov > targetFov ? currentFov - step : currentFov + step;
}

float FovRenderDelta(float currentFov, float targetFov, float changeSpeed, float ticFrac,
                     bool interpolate)
{
	if (!interpolate)
		return 0.0f;

	// The delta IS a fraction of the sim's next step, so the rendered view can never disagree
	// with where the simulation is heading, and lands exactly on it as ticFrac reaches 1.
	const float stepDelta = FovStepTic(currentFov, targetFov, changeSpeed) - currentFov;

	const float frac = ticFrac < 0.0f ? 0.0f : (ticFrac > 1.0f ? 1.0f : ticFrac);

	return stepDelta * frac;
}

} // namespace zx
