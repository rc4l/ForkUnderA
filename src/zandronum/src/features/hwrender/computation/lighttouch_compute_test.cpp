// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/hwrender/computation/lighttouch_compute.h"

using namespace zx::hwrender;

// [rc4l] WDI04's red torch sits behind a solid wall from subsector 2860, so its links name its own room and never 2860.
TEST(LightTouchCompute, ALightTouchesOnlyWhatItWasLinkedTo)
{
	std::vector<LightTouchRange> ranges;
	std::vector<unsigned> ids;
	ComputeLightTouchLists({ { 40, 7, 7, 12 }, {}, { 2860 } }, 100, ranges, ids);
	ASSERT_EQ(3u, ranges.size());
	EXPECT_EQ((std::vector<unsigned>{ 7, 12, 40, 2860 }), ids);
	EXPECT_EQ(0u, ranges[0].offset); EXPECT_EQ(3u, ranges[0].count);
	EXPECT_EQ(3u, ranges[1].offset); EXPECT_EQ(0u, ranges[1].count);
	EXPECT_EQ(3u, ranges[2].offset); EXPECT_EQ(1u, ranges[2].count);
	for (unsigned id : { 7u, 12u, 40u }) EXPECT_TRUE(ComputeLightTouches(ids, ranges[0], id)) << id;
	for (unsigned id : { 0u, 8u, 13u, 41u, 2860u }) EXPECT_FALSE(ComputeLightTouches(ids, ranges[0], id)) << id;
	EXPECT_FALSE(ComputeLightTouches(ids, ranges[1], 7));
	EXPECT_TRUE(ComputeLightTouches(ids, ranges[2], 2860));
	EXPECT_FALSE(ComputeLightTouches(ids, ranges[2], 40));
}

TEST(LightTouchCompute, ALightPastTheCapacityTouchesEverythingAndTheRestStillFit)
{
	std::vector<LightTouchRange> ranges;
	std::vector<unsigned> ids;
	ComputeLightTouchLists({ { 1, 2 }, { 3, 4, 5 }, { 6 } }, 3, ranges, ids);
	EXPECT_EQ((std::vector<unsigned>{ 1, 2, 6 }), ids);
	EXPECT_EQ(kLightTouchesAll, ranges[1].count);
	EXPECT_TRUE(ComputeLightTouches(ids, ranges[1], 999));
	EXPECT_TRUE(ComputeLightTouches(ids, ranges[2], 6));
}

TEST(LightTouchCompute, ListsStartAgainEachCall)
{
	std::vector<LightTouchRange> ranges = { { 5, 5 } };
	std::vector<unsigned> ids = { 9, 9, 9 };
	ComputeLightTouchLists({}, 10, ranges, ids);
	EXPECT_TRUE(ranges.empty());
	EXPECT_TRUE(ids.empty());
}
