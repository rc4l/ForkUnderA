// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-replication/computation/actorstate_compute.h"

#include <gtest/gtest.h>

using namespace zx::netreplication;

TEST(ComputeSendsWoundedHealth, AWoundedMonsterOnly)
{
	// [rc4l] farhitscan's wounded monsters read full health on the shooter and the observer while the server had them at 5 to 12.
	EXPECT_TRUE(ComputeSendsWoundedHealth(false, 6));
	EXPECT_TRUE(ComputeSendsWoundedHealth(false, 1));
	EXPECT_FALSE(ComputeSendsWoundedHealth(false, 0));
	EXPECT_FALSE(ComputeSendsWoundedHealth(false, -10));
	EXPECT_FALSE(ComputeSendsWoundedHealth(true, 60));
}

TEST(ComputeAddPendingHealth, EachActorOncePerTic)
{
	std::vector<int> pending;
	EXPECT_TRUE(ComputeAddPendingHealth(pending, 12));
	for (int pellet = 1; pellet < 7; pellet++) EXPECT_FALSE(ComputeAddPendingHealth(pending, 12));
	EXPECT_TRUE(ComputeAddPendingHealth(pending, 40));
	EXPECT_EQ(pending, (std::vector<int>{ 12, 40 }));
}

TEST(ComputeAddPendingHealth, AnActorWithoutANetworkIdIsNeverSent)
{
	std::vector<int> pending;
	EXPECT_FALSE(ComputeAddPendingHealth(pending, 0));
	EXPECT_FALSE(ComputeAddPendingHealth(pending, -1));
	EXPECT_TRUE(pending.empty());
}

// [Doruk-Aksoy] An Arachnotron killed between its step onto the -16 step and the raise sent its death at -32, and every client kept it a step low.
TEST(ComputeSettledDeathZ, AWalkerBelowTheFloorItSteppedOntoIsSentStandingOnIt)
{
	const int64_t UNIT = 65536;
	EXPECT_EQ(-16 * UNIT, ComputeSettledDeathZ(-32 * UNIT, -16 * UNIT, false));
}

// [Doruk-Aksoy] Standing on its floor or above it, falling, a death goes out where it is, and one without gravity is never moved.
TEST(ComputeSettledDeathZ, OnOrAboveTheFloorOrWithoutGravityItIsWhereItIs)
{
	const int64_t UNIT = 65536;
	EXPECT_EQ(-16 * UNIT, ComputeSettledDeathZ(-16 * UNIT, -16 * UNIT, false));
	EXPECT_EQ(40 * UNIT, ComputeSettledDeathZ(40 * UNIT, 0, false));
	EXPECT_EQ(-32 * UNIT, ComputeSettledDeathZ(-32 * UNIT, -16 * UNIT, true));
}

// [rc4l] A puff jumping straight to its crash, death or melee state goes with no net id, whatever its sounds.
TEST(ComputePuffSend, AnyStateButSpawnGoesWithNoNetId)
{
	EXPECT_EQ(kPuffNoNetID, ComputePuffSend(false, true, true, true));
	EXPECT_EQ(kPuffNoNetID, ComputePuffSend(false, false, false, false));
}

TEST(ComputePuffSend, SoundsAndATemporaryObituaryNeedANetId)
{
	EXPECT_EQ(kPuffWithNetID, ComputePuffSend(true, true, false, false));
	EXPECT_EQ(kPuffWithNetID, ComputePuffSend(true, false, true, false));
	EXPECT_EQ(kPuffWithNetID, ComputePuffSend(true, false, false, true));
	EXPECT_EQ(kPuffEachClient, ComputePuffSend(true, false, false, false));
}

// [rc4l] Only the shooter predicting its own NONETID puff already has it.
TEST(ComputeSkipsPuffFor, OnlyTheShooterPredictingANoNetIdPuff)
{
	EXPECT_TRUE(ComputeSkipsPuffFor(true, true, true));
	EXPECT_FALSE(ComputeSkipsPuffFor(false, true, true));
	EXPECT_FALSE(ComputeSkipsPuffFor(true, false, true));
	EXPECT_FALSE(ComputeSkipsPuffFor(true, true, false));
}
