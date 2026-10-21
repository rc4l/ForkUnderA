// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/levelmesh/computation/wallglow_compute.h"

using namespace zx::levelmesh;

TEST(WallGlow, AWallDrawnFullbrightTakesNoGlow)
{
	EXPECT_FALSE(ComputeWallTakesGlow(0xffffff, 255));
	EXPECT_TRUE(ComputeWallTakesGlow(0xffffff, 254));
	EXPECT_TRUE(ComputeWallTakesGlow(0xff8080, 255));
	// [rc4l] The alpha byte of a PalEntry is not part of the colour.
	EXPECT_FALSE(ComputeWallTakesGlow(0xffffffffu, 255));
}

// [rc4l] A flat floor at 16 (ic 1 and d -16, as secplane_t keeps one facing up) and a flat ceiling at 128 (ic -1 and d 128, facing down).
TEST(WallGlow, DistancesAreMeasuredFromThePlanesAtThePoint)
{
	const float floorPlane[4] = { 0.f, 0.f, 1.f, -16.f };
	const float ceilingPlane[4] = { 0.f, 0.f, -1.f, 128.f };
	float below, above;
	ComputeGlowDistances(ceilingPlane, floorPlane, 64.f, 40.f, 32.f, below, above);
	EXPECT_FLOAT_EQ(88.f, below);
	EXPECT_FLOAT_EQ(24.f, above);
	// [rc4l] A sloped floor rises with x.
	const float slope[4] = { -0.5f, 0.f, 1.f, 0.f };
	ComputeGlowDistances(ceilingPlane, slope, 20.f, 40.f, 0.f, below, above);
	EXPECT_FLOAT_EQ(30.f, above);
}

TEST(WallGlow, TheGlowFadesOverItsHeight)
{
	EXPECT_FLOAT_EQ(1.f, ComputeGlowFactor(64.f, 0.f));
	EXPECT_FLOAT_EQ(0.75f, ComputeGlowFactor(64.f, 16.f));
	EXPECT_FLOAT_EQ(0.f, ComputeGlowFactor(64.f, 64.f));
	EXPECT_FLOAT_EQ(0.f, ComputeGlowFactor(64.f, 100.f));
	EXPECT_FLOAT_EQ(0.f, ComputeGlowFactor(0.f, 0.f));
}
