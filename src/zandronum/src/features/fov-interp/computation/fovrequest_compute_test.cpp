// [rc4l] Tests for fovrequest_compute. 100% coverage of the permission rules.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fovrequest_compute.h"
#include "gtest/gtest.h"

#include <limits>

using namespace zx;

// ---------------------------------------------------------------------------
// FovRequestClamp — the range the old `fov` CCMD enforced, and all DEM_FOV can carry
// ---------------------------------------------------------------------------

TEST(FovRequest, ClampHoldsTheCcmdRange)
{
	EXPECT_EQ(5,   FovRequestClamp(0));
	EXPECT_EQ(5,   FovRequestClamp(-120));
	EXPECT_EQ(5,   FovRequestClamp(5));
	EXPECT_EQ(90,  FovRequestClamp(90));
	EXPECT_EQ(179, FovRequestClamp(179));
	EXPECT_EQ(179, FovRequestClamp(400));
}

// ---------------------------------------------------------------------------
// FovCooldownActive
// ---------------------------------------------------------------------------

TEST(FovRequest, CooldownOfZeroNeverBlocks)
{
	EXPECT_FALSE(FovCooldownActive(100, 100, 0));
	EXPECT_FALSE(FovCooldownActive(100, 100, -5));
}

TEST(FovRequest, CooldownBlocksInsideTheWindow)
{
	EXPECT_TRUE (FovCooldownActive(100, 100, 35));   // same tic
	EXPECT_TRUE (FovCooldownActive(134, 100, 35));   // one tic short
	EXPECT_FALSE(FovCooldownActive(135, 100, 35));   // exactly the gap is allowed
	EXPECT_FALSE(FovCooldownActive(500, 100, 35));
}

TEST(FovRequest, CooldownIgnoresABackwardsClock)
{
	// Map change / demo seek / reconnect can move gametic behind the stored tic; that must not
	// lock the player out until the counter catches back up.
	EXPECT_FALSE(FovCooldownActive(10, 5000, 35));
}

// ---------------------------------------------------------------------------
// FovRequestDecision
// ---------------------------------------------------------------------------

TEST(FovRequest, OrdinaryChangeSetsOwnFov)
{
	EXPECT_EQ(FOV_SET_MINE, FovRequestDecision(false, false, false, 100, 0, 0));
	EXPECT_EQ(FOV_SET_MINE, FovRequestDecision(false, true,  true,  100, 0, 0));
}

TEST(FovRequest, LockedFovDeniesEveryoneButTheArbitrator)
{
	EXPECT_EQ(FOV_DENIED_LOCKED, FovRequestDecision(true, false, true,  100, 0, 0));
	EXPECT_EQ(FOV_DENIED_LOCKED, FovRequestDecision(true, false, false, 100, 0, 0));
}

TEST(FovRequest, ArbitratorUnderALockSetsEveryonesFov)
{
	// Zandronum's existing DEM_FOV behaviour, kept — Q-Zandronum deleted this path entirely.
	EXPECT_EQ(FOV_SET_EVERYONE, FovRequestDecision(true, true, false, 100, 0, 0));
}

TEST(FovRequest, ArbitratorUnderALockIsExemptFromTheCooldown)
{
	// Administrative change, not a player peeking — the rate limit must not swallow it.
	EXPECT_EQ(FOV_SET_EVERYONE, FovRequestDecision(true, true, true, 100, 100, 35));
}

TEST(FovRequest, CooldownAppliesToClients)
{
	EXPECT_EQ(FOV_DENIED_COOLDOWN, FovRequestDecision(false, false, true, 100, 100, 35));
	EXPECT_EQ(FOV_SET_MINE,        FovRequestDecision(false, false, true, 200, 100, 35));
}

TEST(FovRequest, CooldownDoesNotApplyOffline)
{
	// No server enforcing it and no opponent to gain an advantage over.
	EXPECT_EQ(FOV_SET_MINE, FovRequestDecision(false, false, false, 100, 100, 35));
}

TEST(FovRequest, LockOutranksCooldownAsTheReportedReason)
{
	// Both rules would refuse; the lock is the honest explanation to print.
	EXPECT_EQ(FOV_DENIED_LOCKED, FovRequestDecision(true, false, true, 100, 100, 35));
}

// ---------------------------------------------------------------------------
// FovOnSpawn — respawn keeps the player's own FOV, and only theirs
// ---------------------------------------------------------------------------

TEST(FovRequest, SpawnRestoresTheLocalPlayersChoice)
{
	EXPECT_FLOAT_EQ(110.0f, FovOnSpawn(true, false, 110.0f, 5.f, 179.f));
}

TEST(FovRequest, SpawnLeavesOtherPlayersAtTheDefault)
{
	// `fov` is a client CVAR: it is this machine's preference, never another player's.
	EXPECT_FLOAT_EQ(90.0f, FovOnSpawn(false, false, 110.0f, 5.f, 179.f));
}

TEST(FovRequest, ServerNeverStampsItsOwnFovOnAnyone)
{
	// The bug in the reference implementation: it used the local CVAR for every player spawned,
	// so a server's own fov setting would land on all of them.
	EXPECT_FLOAT_EQ(90.0f, FovOnSpawn(true,  true, 110.0f, 5.f, 179.f));
	EXPECT_FLOAT_EQ(90.0f, FovOnSpawn(false, true, 110.0f, 5.f, 179.f));
}

TEST(FovRequest, SpawnClampsAHandEditedConfig)
{
	EXPECT_FLOAT_EQ(5.0f,   FovOnSpawn(true, false, 1.0f, 5.f, 179.f));
	EXPECT_FLOAT_EQ(179.0f, FovOnSpawn(true, false, 900.0f, 5.f, 179.f));
}

// ---------------------------------------------------------------------------
// The server's sv_minfov and sv_maxfov (zandronum@7b3e7adb1460)
// ---------------------------------------------------------------------------

TEST(FovRequest, SpawnStaysWithinTheServersLimits)
{
	// [rc4l] Everyone else's 90 and the local player's own choice are both held inside the limits.
	EXPECT_FLOAT_EQ(100.0f, FovOnSpawn(false, false, 110.0f, 100.f, 120.f));
	EXPECT_FLOAT_EQ(100.0f, FovOnSpawn(true,  true,  110.0f, 100.f, 120.f));
	EXPECT_FLOAT_EQ(110.0f, FovOnSpawn(true,  false, 110.0f, 100.f, 120.f));
	EXPECT_FLOAT_EQ(80.0f,  FovOnSpawn(true,  false, 110.0f, 60.f,  80.f));
}

TEST(FovRequest, ServerLimitsClampEitherSide)
{
	EXPECT_FLOAT_EQ(60.0f,  ComputeFovWithinServerLimits(30.f,  60.f, 120.f));
	EXPECT_FLOAT_EQ(120.0f, ComputeFovWithinServerLimits(150.f, 60.f, 120.f));
	EXPECT_FLOAT_EQ(90.5f,  ComputeFovWithinServerLimits(90.5f, 60.f, 120.f));
	EXPECT_FLOAT_EQ(60.0f,  ComputeFovWithinServerLimits(60.f,  60.f, 120.f));
}

TEST(FovRequest, LimitWithinRangeAndOrderIsKept)
{
	const FovLimitFix min = ComputeFovLimit(60.f, 120.f, true, false);
	EXPECT_EQ(FOV_LIMIT_KEPT, min.verdict);
	EXPECT_FLOAT_EQ(60.f, min.value);
	const FovLimitFix max = ComputeFovLimit(120.f, 60.f, false, false);
	EXPECT_EQ(FOV_LIMIT_KEPT, max.verdict);
	EXPECT_FLOAT_EQ(120.f, max.value);
	// [rc4l] The ends themselves are allowed.
	EXPECT_EQ(FOV_LIMIT_KEPT, ComputeFovLimit(5.f, 179.f, true, false).verdict);
	EXPECT_EQ(FOV_LIMIT_KEPT, ComputeFovLimit(179.f, 5.f, false, false).verdict);
}

TEST(FovRequest, LimitOutsideTheRangeMovesToItsEnd)
{
	const FovLimitFix min = ComputeFovLimit(1.f, 179.f, true, false);
	EXPECT_EQ(FOV_LIMIT_RANGED, min.verdict);
	EXPECT_FLOAT_EQ(5.f, min.value);
	const FovLimitFix max = ComputeFovLimit(200.f, 5.f, false, false);
	EXPECT_EQ(FOV_LIMIT_RANGED, max.verdict);
	EXPECT_FLOAT_EQ(179.f, max.value);
}

TEST(FovRequest, LimitThatIsNotANumberMovesToItsEnd)
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	EXPECT_FLOAT_EQ(5.f,   ComputeFovLimit(nan, 179.f, true,  false).value);
	EXPECT_FLOAT_EQ(179.f, ComputeFovLimit(nan, 5.f,   false, false).value);
}

TEST(FovRequest, LimitCrossingTheOtherMovesOneDegreeInside)
{
	// [rc4l] Equal counts as crossing, so the two limits never meet.
	const FovLimitFix min = ComputeFovLimit(120.f, 120.f, true, false);
	EXPECT_EQ(FOV_LIMIT_CROSSED, min.verdict);
	EXPECT_FLOAT_EQ(119.f, min.value);
	const FovLimitFix max = ComputeFovLimit(50.f, 60.f, false, false);
	EXPECT_EQ(FOV_LIMIT_CROSSED, max.verdict);
	EXPECT_FLOAT_EQ(61.f, max.value);
}

TEST(FovRequest, ClientKeepsWhatTheServerSent)
{
	// [rc4l] The server's new minimum can arrive above this client's old maximum before the new maximum does.
	const FovLimitFix min = ComputeFovLimit(130.f, 120.f, true, true);
	EXPECT_EQ(FOV_LIMIT_KEPT, min.verdict);
	EXPECT_FLOAT_EQ(130.f, min.value);
	EXPECT_EQ(FOV_LIMIT_KEPT, ComputeFovLimit(1.f, 120.f, true, true).verdict);
}
