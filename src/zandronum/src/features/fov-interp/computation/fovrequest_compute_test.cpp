// [rc4l] Tests for fovrequest_compute. 100% coverage of the permission rules.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fovrequest_compute.h"
#include "gtest/gtest.h"

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
	EXPECT_FLOAT_EQ(110.0f, FovOnSpawn(true, false, 110.0f));
}

TEST(FovRequest, SpawnLeavesOtherPlayersAtTheDefault)
{
	// `fov` is a client CVAR: it is this machine's preference, never another player's.
	EXPECT_FLOAT_EQ(90.0f, FovOnSpawn(false, false, 110.0f));
}

TEST(FovRequest, ServerNeverStampsItsOwnFovOnAnyone)
{
	// The bug in the reference implementation: it used the local CVAR for every player spawned,
	// so a server's own fov setting would land on all of them.
	EXPECT_FLOAT_EQ(90.0f, FovOnSpawn(true,  true, 110.0f));
	EXPECT_FLOAT_EQ(90.0f, FovOnSpawn(false, true, 110.0f));
}

TEST(FovRequest, SpawnClampsAHandEditedConfig)
{
	EXPECT_FLOAT_EQ(5.0f,   FovOnSpawn(true, false, 1.0f));
	EXPECT_FLOAT_EQ(179.0f, FovOnSpawn(true, false, 900.0f));
}
