// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/aspect-ratio/computation/ratiobucket_compute.h"

#include <gtest/gtest.h>

using namespace zx::aspect;

TEST(RatioBucket, CommonModesFallInTheirBuckets)
{
	EXPECT_EQ(0, ComputeRatioBucket(640, 480, false));
	EXPECT_EQ(1, ComputeRatioBucket(1920, 1080, false));
	EXPECT_EQ(2, ComputeRatioBucket(1680, 1050, false));
	EXPECT_EQ(3, ComputeRatioBucket(1360, 800, false));
}

TEST(RatioBucket, TheTwoOldSixteenTenSizesAreFourThree)
{
	EXPECT_EQ(0, ComputeRatioBucket(320, 200, false));
	EXPECT_EQ(0, ComputeRatioBucket(640, 400, false));
}

TEST(RatioBucket, FiveFourOnlyUnderTft)
{
	EXPECT_EQ(0, ComputeRatioBucket(1280, 1024, false));
	EXPECT_EQ(4, ComputeRatioBucket(1280, 1024, true));
}

TEST(RatioBucket, UltrawideModesAreTwentyOneNine)
{
	// [rc4l] 64:27 within 30 pixels, so the marketed 3440x1440 counts with the exact 2560x1080.
	EXPECT_EQ(6, ComputeRatioBucket(2560, 1080, false));
	EXPECT_EQ(6, ComputeRatioBucket(3440, 1440, false));
	EXPECT_EQ(6, ComputeRatioBucket(1280, 540, false));
	// [rc4l] A super-ultrawide 32:9 is none of them, so it stays 4:3 as every unknown shape does.
	EXPECT_EQ(0, ComputeRatioBucket(5120, 1440, false));
}

TEST(RatioBucket, VidAspectForcesItsBucket)
{
	EXPECT_EQ(-1, ComputeForcedRatioBucket(0, false, false, 1920, 1080));
	EXPECT_EQ(1, ComputeForcedRatioBucket(1, false, false, 640, 480));
	EXPECT_EQ(2, ComputeForcedRatioBucket(2, false, false, 640, 480));
	EXPECT_EQ(0, ComputeForcedRatioBucket(3, false, false, 1920, 1080));
	EXPECT_EQ(4, ComputeForcedRatioBucket(4, false, false, 640, 480));
	EXPECT_EQ(3, ComputeForcedRatioBucket(5, false, false, 640, 480));
	EXPECT_EQ(6, ComputeForcedRatioBucket(6, false, false, 640, 480));
	EXPECT_EQ(-1, ComputeForcedRatioBucket(7, false, false, 640, 480));
}

TEST(RatioBucket, NoWidescreenWinsOverVidAspect)
{
	EXPECT_EQ(0, ComputeForcedRatioBucket(6, true, false, 1280, 1024));
	EXPECT_EQ(4, ComputeForcedRatioBucket(6, true, true, 1280, 1024));
	EXPECT_EQ(0, ComputeForcedRatioBucket(0, true, true, 1920, 1080));
}

TEST(RatioBucket, WidescreenBuckets)
{
	EXPECT_FALSE(ComputeRatioIsWidescreen(0));
	EXPECT_TRUE(ComputeRatioIsWidescreen(1));
	EXPECT_TRUE(ComputeRatioIsWidescreen(2));
	EXPECT_TRUE(ComputeRatioIsWidescreen(3));
	EXPECT_FALSE(ComputeRatioIsWidescreen(4));
	EXPECT_TRUE(ComputeRatioIsWidescreen(6));
}
