// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/mapangle_compute.h"

#include <gtest/gtest.h>

using zx::map::ComputeMapThingAngle;

TEST(MapThingAngle, CompassPointsAreTheirBinaryAngles)
{
	EXPECT_EQ(0u, ComputeMapThingAngle(0));
	EXPECT_EQ(0x20000000u, ComputeMapThingAngle(45));
	EXPECT_EQ(0x40000000u, ComputeMapThingAngle(90));
	EXPECT_EQ(0x80000000u, ComputeMapThingAngle(180));
	EXPECT_EQ(0xC0000000u, ComputeMapThingAngle(270));
}

TEST(MapThingAngle, DegreesBetweenThemKeepTheirOwnAngle)
{
	// [rc4l] 30 degrees, which the old ANG45 * (30 / 45) turned to 0.
	EXPECT_EQ(357913941u, ComputeMapThingAngle(30));
	EXPECT_EQ(11930464u, ComputeMapThingAngle(1));
	EXPECT_EQ(0xFF49F49Fu, ComputeMapThingAngle(359));
}

TEST(MapThingAngle, AFullTurnAndNegativeDegreesWrap)
{
	EXPECT_EQ(0u, ComputeMapThingAngle(360));
	EXPECT_EQ(0x20000000u, ComputeMapThingAngle(405));
	EXPECT_EQ(0xC0000000u, ComputeMapThingAngle(-90));
}
