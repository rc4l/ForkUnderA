// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/actor-flags/computation/actorflags_compute.h"

using namespace zx::actorflags;

TEST(ActorFlags, AFlagNameSplitsAtItsFirstDot)
{
	const FlagName bare = ComputeFlagName("SHOOTABLE");
	EXPECT_EQ(bare.part1, "SHOOTABLE");
	EXPECT_EQ(bare.part2, "");
	EXPECT_FALSE(bare.dotted);
	const FlagName dotted = ComputeFlagName("Inventory.UNDROPPABLE");
	EXPECT_EQ(dotted.part1, "Inventory");
	EXPECT_EQ(dotted.part2, "UNDROPPABLE");
	EXPECT_TRUE(dotted.dotted);
	const FlagName twice = ComputeFlagName("a.b.c");
	EXPECT_EQ(twice.part1, "a");
	EXPECT_EQ(twice.part2, "b.c");
	EXPECT_EQ(ComputeFlagName(nullptr).part1, "");
	EXPECT_FALSE(ComputeFlagName(nullptr).dotted);
}

TEST(ActorFlags, OnlyALivingKillCountsTowardTheMapsKills)
{
	EXPECT_TRUE(ComputeCountsKill(true, 20));
	EXPECT_FALSE(ComputeCountsKill(true, 0));
	EXPECT_FALSE(ComputeCountsKill(true, -5));
	EXPECT_FALSE(ComputeCountsKill(false, 20));
}

TEST(ActorFlags, AMapTotalMovesOnlyWhenCountingChanges)
{
	EXPECT_EQ(ComputeCountDelta(false, true), 1);
	EXPECT_EQ(ComputeCountDelta(true, false), -1);
	EXPECT_EQ(ComputeCountDelta(true, true), 0);
	EXPECT_EQ(ComputeCountDelta(false, false), 0);
}
