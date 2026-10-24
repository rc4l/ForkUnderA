// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "animmarks_compute.h"

#include <gtest/gtest.h>

using namespace zx::caching;

TEST(AnimMarks, ARangeAnimationShowsTheTexturesFromItsBaseOn)
{
	EXPECT_EQ((std::vector<int>{ 10, 11, 12 }), ComputeAnimationFrames(false, 10, 3, {}, 100));
	EXPECT_EQ((std::vector<int>{ 98, 99 }), ComputeAnimationFrames(false, 98, 4, {}, 100));
}

TEST(AnimMarks, ADiscreteAnimationShowsItsListedFramesWithinTheTextures)
{
	EXPECT_EQ((std::vector<int>{ 40, 7 }), ComputeAnimationFrames(true, 5, 4, { 40, 7, -1 }, 100));
	EXPECT_EQ((std::vector<int>{}), ComputeAnimationFrames(true, 5, 1, { 200 }, 100));
}

// [rc4l] WFALL2 was made on the frame the waterfall first turned to it, since only WFALL1 was marked.
TEST(AnimMarks, OneMarkedFrameMarksEveryFrameWithEveryMark)
{
	unsigned char hits[6] = { 0, 1, 0, 2, 0, 0 };
	ComputeSpreadMarks(hits, 6, { 1, 2, 3, 9 });
	EXPECT_EQ(0, hits[0]);
	EXPECT_EQ(3, hits[1]);
	EXPECT_EQ(3, hits[2]);
	EXPECT_EQ(3, hits[3]);
	EXPECT_EQ(0, hits[4]);
}

TEST(AnimMarks, AnUnmarkedAnimationStaysUnmarked)
{
	unsigned char hits[4] = { 0, 0, 0, 0 };
	ComputeSpreadMarks(hits, 4, { 0, 1, -1 });
	EXPECT_EQ(0, hits[0]);
	EXPECT_EQ(0, hits[1]);
}
