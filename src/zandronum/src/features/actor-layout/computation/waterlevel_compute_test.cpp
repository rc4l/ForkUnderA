// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/actor-layout/computation/waterlevel_compute.h"
#include <gtest/gtest.h>

// [Doruk-Aksoy] A dry actor stays at zero and never splashes.
TEST(ComputeWaterStore, DryStaysDry)
{
	const WaterStore s = ComputeWaterStore(0, 0, 0, false, true);
	EXPECT_EQ(s.water, 0);
	EXPECT_EQ(s.boom, 0);
	EXPECT_FALSE(s.splash);
}

// [Doruk-Aksoy] Entering water splashes once, and only when asked to.
TEST(ComputeWaterStore, EnteringSplashesWhenAsked)
{
	WaterStore s = ComputeWaterStore(0, 0, 2, false, true);
	EXPECT_EQ(s.water, 2);
	EXPECT_EQ(s.boom, 2);
	EXPECT_TRUE(s.splash);
	EXPECT_FALSE(ComputeWaterStore(0, 0, 2, false, false).splash);
	EXPECT_FALSE(ComputeWaterStore(2, 2, 3, false, true).splash);
}

// [Doruk-Aksoy] A non-swimmable sector keeps the old waterlevel but records the measured level for the next splash.
TEST(ComputeWaterStore, ResetKeepsTheOldLevel)
{
	const WaterStore s = ComputeWaterStore(1, 0, 3, true, true);
	EXPECT_EQ(s.water, 1);
	EXPECT_EQ(s.boom, 3);
	EXPECT_TRUE(s.splash);
}
