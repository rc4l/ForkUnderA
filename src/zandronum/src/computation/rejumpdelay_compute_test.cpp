// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "computation/rejumpdelay_compute.h"

using namespace zx;

TEST(RejumpDelay, ATakeoffKeepsTheEnginesTicsUntilARejumpDelayIsSet)
{
	EXPECT_EQ(ComputeTakeoffJumpTics(-1, false, 35), -1);
	EXPECT_EQ(ComputeTakeoffJumpTics(-1, true, 35), 18);
	EXPECT_EQ(ComputeTakeoffJumpTics(0, true, 35), 0);
	EXPECT_EQ(ComputeTakeoffJumpTics(30, false, 35), 0);
}

TEST(RejumpDelay, ALandingAlwaysDelaysWithARejumpDelayElseByTheEnginesRules)
{
	EXPECT_FALSE(ComputeLandingDelaysJump(-1, false, 0, false));
	EXPECT_TRUE(ComputeLandingDelaysJump(-1, true, 0, false));
	EXPECT_TRUE(ComputeLandingDelaysJump(-1, false, -1, false));
	EXPECT_TRUE(ComputeLandingDelaysJump(-1, false, 0, true));
	EXPECT_TRUE(ComputeLandingDelaysJump(0, false, 0, false));
	EXPECT_EQ(ComputeLandingJumpTics(-1), 7);
	EXPECT_EQ(ComputeLandingJumpTics(0), 0);
	EXPECT_EQ(ComputeLandingJumpTics(30), 30);
}

TEST(RejumpDelay, RisingOnTheFloorWaitsAtLeastTwoTicsAndOnlyForAPlayer)
{
	EXPECT_TRUE(ComputeRisingDelaysJump(true, 0, true));
	EXPECT_FALSE(ComputeRisingDelaysJump(false, 0, true));
	EXPECT_FALSE(ComputeRisingDelaysJump(true, -1, true));
	EXPECT_FALSE(ComputeRisingDelaysJump(true, 5, false));
	EXPECT_EQ(ComputeRisingJumpTics(0), 2);
	EXPECT_EQ(ComputeRisingJumpTics(1), 2);
	EXPECT_EQ(ComputeRisingJumpTics(30), 30);
}
