// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/flood_compute.h"

using namespace zx::hwrender;

// [rc4l] wdiexsec: a step with no lower texture showed the level behind it on Vulkan, where GL paints the gap with the higher floor seen through it.
TEST(Flood, ALowerGapSeenFromAboveShowsTheHigherFloorBetweenTheEyeAndTheWall)
{
	float x = 0.f, y = 0.f;
	// [rc4l] Eye at height 41 over the low floor, looking at a step face from 0 to 32 on the line y = 128.
	ASSERT_TRUE(ComputeFloodPoint(0.f, 0.f, 41.f, 0.f, 128.f, 0.f, 32.f, x, y));
	EXPECT_FLOAT_EQ(0.f, x);
	EXPECT_NEAR(128.f * 9.f / 41.f, y, 1e-3f);
	// [rc4l] The top of the gap is the plane itself, so the point is the fragment.
	ASSERT_TRUE(ComputeFloodPoint(0.f, 0.f, 41.f, 16.f, 128.f, 32.f, 32.f, x, y));
	EXPECT_FLOAT_EQ(16.f, x);
	EXPECT_FLOAT_EQ(128.f, y);
}

TEST(Flood, AnUpperGapSeenFromBelowShowsTheLowerCeiling)
{
	float x = 0.f, y = 0.f;
	ASSERT_TRUE(ComputeFloodPoint(0.f, 0.f, 41.f, 64.f, 0.f, 160.f, 128.f, x, y));
	EXPECT_NEAR(64.f * 87.f / 119.f, x, 1e-3f);
}

// [rc4l] GL floods only when the plane faces the eye, so an eye on the plane's far side, or level with the fragment, gets nothing.
TEST(Flood, NoPointWhenThePlaneIsNotBetweenTheEyeAndTheFragment)
{
	float x = 0.f, y = 0.f;
	EXPECT_FALSE(ComputeFloodPoint(0.f, 0.f, 20.f, 0.f, 128.f, 10.f, 32.f, x, y));
	EXPECT_FALSE(ComputeFloodPoint(0.f, 0.f, 20.f, 0.f, 128.f, 25.f, 32.f, x, y));
	EXPECT_FALSE(ComputeFloodPoint(0.f, 0.f, 20.f, 0.f, 128.f, 20.f, 32.f, x, y));
}
