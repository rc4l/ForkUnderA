// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/aspect-ratio/computation/ratiobucket_compute.h"

#include <gtest/gtest.h>

using namespace zx::aspect;

TEST(RatioBucket, CommonModesFallInTheirBuckets)
{
	EXPECT_EQ(0, ComputeRatioBucket(640, 480));
	EXPECT_EQ(1, ComputeRatioBucket(1920, 1080));
	EXPECT_EQ(2, ComputeRatioBucket(1680, 1050));
	EXPECT_EQ(3, ComputeRatioBucket(1360, 800));
	EXPECT_EQ(4, ComputeRatioBucket(1280, 1024));
}

TEST(RatioBucket, TheOldSixteenTenSizesAreNowSixteenTen)
{
	// [rc4l] The nearest ratio has no exceptions, so 320x200 is the 16:10 it measures.
	EXPECT_EQ(2, ComputeRatioBucket(320, 200));
	EXPECT_EQ(2, ComputeRatioBucket(640, 400));
}

TEST(RatioBucket, EveryShapeTakesTheNearest)
{
	EXPECT_EQ(6, ComputeRatioBucket(2560, 1080));
	EXPECT_EQ(6, ComputeRatioBucket(3440, 1440));
	EXPECT_EQ(6, ComputeRatioBucket(1280, 540));
	// [rc4l] Wider than 21:9, and taller than 5:4, still land on the nearest end.
	EXPECT_EQ(6, ComputeRatioBucket(5120, 1440));
	EXPECT_EQ(4, ComputeRatioBucket(768, 1024));
	// [rc4l] 3:2 sits nearer 16:10 than 4:3.
	EXPECT_EQ(2, ComputeRatioBucket(1440, 960));
}

TEST(RatioBucket, NoHeightIsFourThree)
{
	EXPECT_EQ(0, ComputeRatioBucket(640, 0));
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

TEST(RatioBucket, MenuFilterTakesEveryBucketToTwentyOneNine)
{
	EXPECT_EQ(-1, ComputeMenuRatioFilter(-1, false));
	EXPECT_EQ(-1, ComputeMenuRatioFilter(7, false));
	EXPECT_EQ(-1, ComputeMenuRatioFilter(-2, false));
	EXPECT_EQ(1, ComputeMenuRatioFilter(1, false));
	EXPECT_EQ(6, ComputeMenuRatioFilter(6, false));
	// [rc4l] 5:4 is offered only to a TFT, and otherwise falls back to 4:3.
	EXPECT_EQ(0, ComputeMenuRatioFilter(4, false));
	EXPECT_EQ(4, ComputeMenuRatioFilter(4, true));
}
