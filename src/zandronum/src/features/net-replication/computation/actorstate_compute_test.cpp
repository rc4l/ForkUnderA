// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-replication/computation/actorstate_compute.h"

#include <gtest/gtest.h>

using namespace zx::netreplication;

// [Doruk-Aksoy] Seen 2026-11-26 at legacy suppression 2: farhitscan's killed monster read 10 on clients, the server -5, with KillThing suppressed.
TEST(ComputeSendsDeathHealth, AMonsterWhoseKillThingIsSuppressedSendsItsDeadHealth)
{
	EXPECT_TRUE(ComputeSendsDeathHealth(false, true));
}

// [Doruk-Aksoy] KillThing carries it when it goes out, and a player's death is its own command.
TEST(ComputeSendsDeathHealth, NothingWhenKillThingGoesOrForAPlayer)
{
	EXPECT_FALSE(ComputeSendsDeathHealth(false, false));
	EXPECT_FALSE(ComputeSendsDeathHealth(true, true));
	EXPECT_FALSE(ComputeSendsDeathHealth(true, false));
}

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

// [Doruk-Aksoy] Seen in netjoinstomp: a joiner's telefrag threw a corpse still solid inside it, the server's stopped dead, and a client its own block missed kept it a 32-unit step off.
TEST(ComputeSendsBlockedStop, AMovingNonPlayerStoppedByABlockSendsWhereItStopped)
{
	EXPECT_TRUE(ComputeSendsBlockedStop(false, true));
}

// [Doruk-Aksoy] A player's place goes with its own movement commands, and a mover already at rest changed nothing a client holds.
TEST(ComputeSendsBlockedStop, APlayerOrAMoverAlreadyAtRestSendsNothing)
{
	EXPECT_FALSE(ComputeSendsBlockedStop(true, true));
	EXPECT_FALSE(ComputeSendsBlockedStop(false, false));
	EXPECT_FALSE(ComputeSendsBlockedStop(true, false));
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

// [Doruk-Aksoy] A joiner kept a translated class's translation when the server had cleared it to 0, since only a non-zero one was sent.
TEST(ComputeClientAppliedFlags, TheServersBitsButTheClientsOwn)
{
	EXPECT_EQ(0x6u, ComputeClientAppliedFlags(0x1, 0x6, 0));
	EXPECT_EQ(0x7u, ComputeClientAppliedFlags(0x1, 0x6, 0x1));
	EXPECT_EQ(0x6u, ComputeClientAppliedFlags(0x0, 0x7, 0x1));
	EXPECT_EQ(0x0u, ComputeClientAppliedFlags(0xF, 0x0, 0));
}

TEST(ComputeJoinSendsTranslation, WhateverDiffersFromTheClass)
{
	EXPECT_TRUE(ComputeJoinSendsTranslation(0, 0x30007));
	EXPECT_TRUE(ComputeJoinSendsTranslation(0x30007, 0));
	EXPECT_FALSE(ComputeJoinSendsTranslation(0x30007, 0x30007));
	EXPECT_FALSE(ComputeJoinSendsTranslation(0, 0));
}
