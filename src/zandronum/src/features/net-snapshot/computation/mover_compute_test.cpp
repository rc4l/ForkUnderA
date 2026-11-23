//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: mover_compute_test.cpp
//
// Description: Tests for mover_compute: the join's write order over chains, a
// diamond, two-plane links, a cycle and a map with no links, and the mover
// records' names, narrowing flags, turning actions and compare key.
//
//-----------------------------------------------------------------------------

#include "mover_compute.h"
#include <gtest/gtest.h>

#include <string.h>

using namespace zx::netsnapshot;

// [Kaori-Chani] The link types' plane bits, as p_linkedsectors.cpp's LINK_FLOORBITS and
// LINK_CEILINGBITS give them; only their being apart matters here.
#define MOVERTEST_FLOOR		0x03
#define MOVERTEST_CEILING	0x0C

// [Kaori-Chani] No links: index order, nothing driven.
TEST(SnapshotMover, LinkOrderOfAMapWithNoLinksIsIndexOrder)
{
	int order[8];
	BYTE driven[8];
	EXPECT_EQ(0u, ComputeLinkWriteOrder(4, NULL, 0, MOVERTEST_FLOOR, MOVERTEST_CEILING, order, driven));
	for (int i = 0; i < 4; ++i)
	{
		EXPECT_EQ(i, order[i]) << "slot " << i;
		EXPECT_EQ(0, driven[i]) << "slot " << i;
	}
}

// [Kaori-Chani] 3 drives 1 and 1 drives 0, floor and then ceiling, 2 alone: rounds {2, 3}, {1},
// {0}.
TEST(SnapshotMover, LinkOrderWritesADriverBeforeWhatItDrives)
{
	static const SnapLinkEdge chain[] =
	{
		{ 3, 1, MOVERTEST_FLOOR }, { 1, 0, MOVERTEST_CEILING },
	};
	int order[8];
	BYTE driven[8];
	EXPECT_EQ(0u, ComputeLinkWriteOrder(4, chain, 2, MOVERTEST_FLOOR, MOVERTEST_CEILING, order, driven));
	EXPECT_EQ(2, order[0]);
	EXPECT_EQ(3, order[1]);
	EXPECT_EQ(1, order[2]);
	EXPECT_EQ(0, order[3]);
	EXPECT_EQ(2, driven[0]);
	EXPECT_EQ(1, driven[1]);
	EXPECT_EQ(0, driven[2]);
	EXPECT_EQ(0, driven[3]);
}

// [Kaori-Chani] A diamond, 0 driving 1 and 4, 1 driving 2, 2 and 4 driving 3, which waits for 2;
// 4 drives both of 3's planes by one link.
TEST(SnapshotMover, LinkOrderMakesADiamondWaitForItsLastDriver)
{
	static const SnapLinkEdge diamond[] =
	{
		{ 0, 1, MOVERTEST_FLOOR }, { 0, 4, MOVERTEST_FLOOR }, { 1, 2, MOVERTEST_FLOOR },
		{ 2, 3, MOVERTEST_FLOOR }, { 4, 3, MOVERTEST_FLOOR | MOVERTEST_CEILING },
	};
	int order[8];
	BYTE driven[8];
	EXPECT_EQ(0u, ComputeLinkWriteOrder(5, diamond, 5, MOVERTEST_FLOOR, MOVERTEST_CEILING, order, driven));
	EXPECT_EQ(0, order[0]);
	EXPECT_EQ(1, order[1]);
	EXPECT_EQ(4, order[2]);
	EXPECT_EQ(2, order[3]);
	EXPECT_EQ(3, order[4]);
	EXPECT_EQ(3, driven[3]);
	EXPECT_EQ(0, driven[0]);
}

// [Kaori-Chani] A cycle of 1 and 2, which 0 drives too, driving 3: 0 goes first and the rest never
// free, appended in index order and counted.
TEST(SnapshotMover, LinkOrderCountsWhatACycleLeavesUnfreed)
{
	static const SnapLinkEdge cycle[] =
	{
		{ 1, 2, MOVERTEST_FLOOR }, { 2, 1, MOVERTEST_FLOOR }, { 0, 1, MOVERTEST_FLOOR },
		{ 2, 3, MOVERTEST_CEILING },
	};
	int order[8];
	BYTE driven[8];
	EXPECT_EQ(3u, ComputeLinkWriteOrder(4, cycle, 4, MOVERTEST_FLOOR, MOVERTEST_CEILING, order, driven));
	EXPECT_EQ(0, order[0]);
	EXPECT_EQ(1, order[1]);
	EXPECT_EQ(2, order[2]);
	EXPECT_EQ(3, order[3]);
	EXPECT_EQ(2, driven[3]);
}

// [Kaori-Chani] A link type with neither plane's bits orders without driving.
TEST(SnapshotMover, LinkOrderOfALinkTouchingNeitherPlaneDrivesNothing)
{
	static const SnapLinkEdge neither[] = { { 1, 0, 0x10 } };
	int order[8];
	BYTE driven[8];
	EXPECT_EQ(0u, ComputeLinkWriteOrder(2, neither, 1, MOVERTEST_FLOOR, MOVERTEST_CEILING, order, driven));
	EXPECT_EQ(1, order[0]);
	EXPECT_EQ(0, order[1]);
	EXPECT_EQ(0, driven[0]);
}

// [Kaori-Chani] Every class has a name, and one past them reads "?".
TEST(SnapshotMover, EveryMoverClassHasAName)
{
	static const char* const names[SNAPMV_COUNT] =
	{
		"plat", "floor", "elevator", "pillar", "ceiling", "door", "animdoor", "poly",
	};
	for (BYTE c = 0; c < SNAPMV_COUNT; ++c)
	{
		ASSERT_NE(nullptr, names[c]) << "class " << (int)c;
		EXPECT_STREQ(names[c], ComputeMoverClassName(c)) << "class " << (int)c;
	}
	EXPECT_STREQ("?", ComputeMoverClassName(SNAPMV_COUNT));
	EXPECT_STREQ("?", ComputeMoverClassName(255));
}

TEST(SnapshotMover, NarrowingFlagsFollowTheDrivenPlanes)
{
	EXPECT_EQ(0, ComputeMoverNoFlags(0));
	EXPECT_EQ(SNAPMVF_NOFLOOR, ComputeMoverNoFlags(SNAPMV_PLANE_FLOOR));
	EXPECT_EQ(SNAPMVF_NOCEILING, ComputeMoverNoFlags(SNAPMV_PLANE_CEILING));
	EXPECT_EQ(SNAPMVF_NOFLOOR | SNAPMVF_NOCEILING, ComputeMoverNoFlags(SNAPMV_PLANE_BOTH));
}

TEST(SnapshotMover, OnlyRotateAndSwingTurnAPolyobject)
{
	EXPECT_TRUE(ComputePolyTurns(SNAPPO_ROTATE));
	EXPECT_TRUE(ComputePolyTurns(SNAPPO_SWING));
	EXPECT_FALSE(ComputePolyTurns(SNAPPO_MOVE));
	EXPECT_FALSE(ComputePolyTurns(SNAPPO_MOVETO));
	EXPECT_FALSE(ComputePolyTurns(SNAPPO_SLIDE));
}

// [Kaori-Chani] The key clears the phases and keeps the count and each entry's kind, generation,
// angle, direction, targets, total, wait and close. The kept fields are compared against a copy
// taken before the call rather than against a fill pattern, so the test does not pin any field's
// width: xTarget and yTarget are fixed_t, which is eight bytes here and was four before.
TEST(SnapshotMover, ThePolyCompareKeyClearsThePhasesAndKeepsTheRest)
{
	SnapMoverPoly s;
	memset(&s, 0x5A, sizeof(s));
	s.count = 3;
	const SnapMoverPoly before = s;

	ComputePolyCompareKey(s);

	EXPECT_EQ(0, s.x);
	EXPECT_EQ(0, s.y);
	EXPECT_EQ(0, s.angle);
	EXPECT_EQ(3u, s.count);
	for (unsigned int i = 0; i < SNAPPO_MAXENTRIES; ++i)
	{
		const SnapPolyEntry& e = s.e[i];
		const SnapPolyEntry& b = before.e[i];
		// [Kaori-Chani] Cleared: the phases a comparison must ignore.
		EXPECT_EQ(0, e.speed) << "entry " << i;
		EXPECT_EQ(0, e.dist) << "entry " << i;
		EXPECT_EQ(0, e.tics) << "entry " << i;
		EXPECT_EQ(0, e.xSpeed) << "entry " << i;
		EXPECT_EQ(0, e.ySpeed) << "entry " << i;
		// [Kaori-Chani] Kept exactly as they came in.
		EXPECT_EQ(b.kind, e.kind) << "entry " << i;
		EXPECT_EQ(b.gen, e.gen) << "entry " << i;
		EXPECT_EQ(b.angle, e.angle) << "entry " << i;
		EXPECT_EQ(b.direction, e.direction) << "entry " << i;
		EXPECT_EQ(b.xTarget, e.xTarget) << "entry " << i;
		EXPECT_EQ(b.yTarget, e.yTarget) << "entry " << i;
		EXPECT_EQ(b.totalDist, e.totalDist) << "entry " << i;
		EXPECT_EQ(b.waitTics, e.waitTics) << "entry " << i;
		EXPECT_EQ(b.close, e.close) << "entry " << i;
	}
}
