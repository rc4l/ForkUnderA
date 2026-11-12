// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/skulltagscore_compute.h"

#include <gtest/gtest.h>

using zx::ComputeScoresSkulltagPoint;

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
