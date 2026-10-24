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
