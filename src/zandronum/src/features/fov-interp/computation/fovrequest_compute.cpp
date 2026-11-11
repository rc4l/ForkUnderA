// [rc4l] Implementation of the FOV-change permission rules. See the header for provenance.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fovrequest_compute.h"

namespace zx
{

static const int FOV_REQUEST_MIN = 5;
static const int FOV_REQUEST_MAX = 179;

int FovRequestClamp(int fov)
{
	if (fov < FOV_REQUEST_MIN)
		return FOV_REQUEST_MIN;
	if (fov > FOV_REQUEST_MAX)
		return FOV_REQUEST_MAX;
	return fov;
}

bool FovCooldownActive(int gametic, int lastChangeTic, int cooldownTics)
{
	if (cooldownTics <= 0)
		return false;

	// A negative elapsed time means the tic counter moved backwards under us (map change,
	// demo seek, a fresh connection). Treat that as "no recent change" rather than locking the
	// player out until the counter catches up.
	const int elapsed = gametic - lastChangeTic;
	if (elapsed < 0)
		return false;

	return elapsed < cooldownTics;
}

FovRequestVerdict FovRequestDecision(bool freeSpectating, bool fovLocked, bool isArbitrator, bool isClient,
                                     int gametic, int lastChangeTic, int cooldownTics)
{
	if (freeSpectating)
		return FOV_SET_FREE_SPECTATOR;

	// The lock is checked first: when a setting controller has disabled FOV changes, that is the
	// reason to report, and the arbitrator's own change is exempt from the rate limit because it
	// is an administrative action, not a player peeking.
	if (fovLocked)
		return isArbitrator ? FOV_SET_EVERYONE : FOV_DENIED_LOCKED;

	// Only clients are rate-limited: a local game has no server enforcing anything and no
	// opponent to gain an advantage over.
	if (isClient && FovCooldownActive(gametic, lastChangeTic, cooldownTics))
		return FOV_DENIED_COOLDOWN;

	return FOV_SET_MINE;
}

// The engine's historic spawn value, and what every non-local player still gets.
static const float FOV_SPAWN_DEFAULT = 90.0f;

float FovOnSpawn(bool isLocalPlayer, bool isServer, float playerFovCvar, float minFov, float maxFov)
{
	// Clamped through the same rule a typed/slid value goes through, so a hand-edited config
	// cannot spawn someone at an FOV the CVAR would have refused.
	const float own = (isServer || !isLocalPlayer)
		? FOV_SPAWN_DEFAULT
		: static_cast<float> (FovRequestClamp (static_cast<int> (playerFovCvar)));
	return ComputeFovWithinServerLimits (own, minFov, maxFov);
}

float ComputeFovWithinServerLimits(float fov, float minFov, float maxFov)
{
	if (fov < minFov)
		return minFov;
	if (fov > maxFov)
		return maxFov;
	return fov;
}

FovLimitFix ComputeFovLimit(float requested, float otherLimit, bool isMin, bool isClient)
{
	const FovLimitFix kept = { FOV_LIMIT_KEPT, requested };
	if (isClient)
		return kept;

	// [rc4l] Written so a NaN fails the range test too.
	const float end = static_cast<float> (isMin ? FOV_REQUEST_MIN : FOV_REQUEST_MAX);
	if (isMin ? !(requested >= end) : !(requested <= end))
	{
		const FovLimitFix ranged = { FOV_LIMIT_RANGED, end };
		return ranged;
	}

	if (isMin ? requested >= otherLimit : requested <= otherLimit)
	{
		const FovLimitFix crossed = { FOV_LIMIT_CROSSED, isMin ? otherLimit - 1.f : otherLimit + 1.f };
		return crossed;
	}
	return kept;
}

} // namespace zx
