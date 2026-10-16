// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/hwrender/computation/lighttouch_compute.h"

using namespace zx::hwrender;

namespace {

// [rc4l] Each light's ids sorted as the cache keeps them, and the pointers ComputeLightTouchLists takes.
std::vector<const std::vector<unsigned> *> Sorted(std::vector<std::vector<unsigned> > &lists)
{
	std::vector<const std::vector<unsigned> *> out;
	for (std::vector<unsigned> &l : lists) { ComputeSortedTouches(l); out.push_back(&l); }
	return out;
}

} // namespace

TEST(LightTouchCompute, ALightsIdsAreSortedOnceEach)
{
	std::vector<unsigned> ids = { 40, 7, 7, 12, 40 };
	ComputeSortedTouches(ids);
	EXPECT_EQ((std::vector<unsigned>{ 7, 12, 40 }), ids);
}

// [rc4l] WDI04's red torch sits behind a solid wall from subsector 2860, so its links name its own room and never 2860.
TEST(LightTouchCompute, ALightTouchesOnlyWhatItWasLinkedTo)
{
	std::vector<LightTouchRange> ranges;
	std::vector<unsigned> ids;
	std::vector<std::vector<unsigned> > lists = { { 40, 7, 7, 12 }, {}, { 2860 } };
	ComputeLightTouchLists(Sorted(lists), 100, ranges, ids);
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
	std::vector<std::vector<unsigned> > lists = { { 1, 2 }, { 3, 4, 5 }, { 6 } };
	ComputeLightTouchLists(Sorted(lists), 3, ranges, ids);
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

TEST(ComputeDynLightColor, AnAdditiveLightIsAFifthAsBright)
{
	float c[3];
	zx::hwrender::ComputeDynLightColor(255, 0, 51, false, 2.f, c);
	EXPECT_FLOAT_EQ(2.f, c[0]); EXPECT_FLOAT_EQ(0.f, c[1]); EXPECT_FLOAT_EQ(0.4f, c[2]);
	zx::hwrender::ComputeDynLightColor(255, 0, 51, true, 2.f, c);
	EXPECT_FLOAT_EQ(0.4f, c[0]);
}

TEST(ComputeLightLinksStale, AnyChangeOfLightPlaceOrReach)
{
	const zx::hwrender::LightLinkKey kept = { 7u, 1, 2, 3, 64.f };
	zx::hwrender::LightLinkKey now = kept;
	EXPECT_FALSE(zx::hwrender::ComputeLightLinksStale(kept, now));
	now.light = 8u; EXPECT_TRUE(zx::hwrender::ComputeLightLinksStale(kept, now)); now = kept;
	now.x = 9; EXPECT_TRUE(zx::hwrender::ComputeLightLinksStale(kept, now)); now = kept;
	now.y = 9; EXPECT_TRUE(zx::hwrender::ComputeLightLinksStale(kept, now)); now = kept;
	now.z = 9; EXPECT_TRUE(zx::hwrender::ComputeLightLinksStale(kept, now)); now = kept;
	now.radius = 65.f; EXPECT_TRUE(zx::hwrender::ComputeLightLinksStale(kept, now));
}

TEST(ComputeLightsDue, OncePerTicAndAgainOnANewLevelOrSwitch)
{
	zx::hwrender::LightsStamp kept = { -1, -1, -1 };
	bool newLevel = false;
	EXPECT_TRUE(zx::hwrender::ComputeLightsDue(kept, 10, 1, 3, newLevel));
	EXPECT_TRUE(newLevel);
	EXPECT_FALSE(zx::hwrender::ComputeLightsDue(kept, 10, 1, 3, newLevel));
	EXPECT_FALSE(newLevel);
	EXPECT_TRUE(zx::hwrender::ComputeLightsDue(kept, 11, 1, 3, newLevel));
	EXPECT_FALSE(newLevel);
	EXPECT_TRUE(zx::hwrender::ComputeLightsDue(kept, 11, 1, 1, newLevel));
	EXPECT_TRUE(zx::hwrender::ComputeLightsDue(kept, 11, 2, 1, newLevel));
	EXPECT_TRUE(newLevel);
}
