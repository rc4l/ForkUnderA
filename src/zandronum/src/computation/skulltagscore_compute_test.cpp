// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/skulltagscore_compute.h"

#include <gtest/gtest.h>

using zx::ComputeScoresSkulltagPoint;
using zx::ComputeAssistPlayer;

TEST(SkulltagScore, ACarrierScoresWhileItsOwnSkullIsHome)
{
	EXPECT_TRUE(ComputeScoresSkulltagPoint(true, false));
	EXPECT_TRUE(ComputeScoresSkulltagPoint(false, false));
}

TEST(SkulltagScore, ATakenSkullStopsTheScoreOnlyWhenTheServerRequiresIt)
{
	EXPECT_FALSE(ComputeScoresSkulltagPoint(true, true));
	EXPECT_TRUE(ComputeScoresSkulltagPoint(false, true));
}

TEST(SkulltagScore, AnAssistGoesToItsPlayerOutsideSkulltagOrWithTheHomeSkullRequired)
{
	EXPECT_EQ(ComputeAssistPlayer(3, 64, false, false), 3u);
	EXPECT_EQ(ComputeAssistPlayer(3, 64, false, true), 3u);
	EXPECT_EQ(ComputeAssistPlayer(3, 64, true, true), 3u);
}

TEST(SkulltagScore, SkulltagWithoutTheHomeSkullGivesNobodyAndStillClears)
{
	EXPECT_EQ(ComputeAssistPlayer(3, 64, true, false), 64u);
	EXPECT_EQ(ComputeAssistPlayer(64, 64, true, false), 64u);
}
