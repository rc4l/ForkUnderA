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

FovRequestVerdict FovRequestDecision(bool fovLocked, bool isArbitrator, bool isClient,
                                     int gametic, int lastChangeTic, int cooldownTics)
{
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

float FovOnSpawn(bool isLocalPlayer, bool isServer, float playerFovCvar)
{
	if (isServer || !isLocalPlayer)
		return FOV_SPAWN_DEFAULT;

	// Clamped through the same rule a typed/slid value goes through, so a hand-edited config
	// cannot spawn someone at an FOV the CVAR would have refused.
	return static_cast<float> (FovRequestClamp (static_cast<int> (playerFovCvar)));
}

} // namespace zx
