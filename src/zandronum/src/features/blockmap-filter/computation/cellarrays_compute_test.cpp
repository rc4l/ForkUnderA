// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/blockmap-filter/computation/cellarrays_compute.h"
#include <gtest/gtest.h>

using namespace zx::cells;

namespace {
int nodes[4];

ThingEntry At(int i, int64_t x)
{
	ThingEntry e = { x, 0, 16, 16, &nodes[i], &nodes[i], false };
	return e;
}

TEST(ComputeCellLink, TheHeadIsTheBack)
{
	std::vector<ThingEntry> cell;
	ComputeCellLink(cell, At(0, 1));
	ComputeCellLink(cell, At(1, 2));
	ASSERT_EQ(cell.size(), 2u);
	EXPECT_EQ(cell.back().node, &nodes[1]);
	EXPECT_EQ(cell.front().node, &nodes[0]);
}

TEST(ComputeCellUnlink, KeepsTheOthersInOrder)
{
	std::vector<ThingEntry> cell;
	for (int i = 0; i < 3; i++) ComputeCellLink(cell, At(i, i));
	EXPECT_TRUE(ComputeCellUnlink(cell, &nodes[1]));
	ASSERT_EQ(cell.size(), 2u);
	EXPECT_EQ(cell[0].node, &nodes[0]);
	EXPECT_EQ(cell[1].node, &nodes[2]);
	EXPECT_FALSE(ComputeCellUnlink(cell, &nodes[3]));
}

TEST(ComputeCellRefresh, RewritesInPlace)
{
	std::vector<ThingEntry> cell;
	for (int i = 0; i < 2; i++) ComputeCellLink(cell, At(i, i));
	EXPECT_TRUE(ComputeCellRefresh(cell, &nodes[0], 7, 8, 9, 10));
	EXPECT_EQ(cell[0].x, 7);
	EXPECT_EQ(cell[0].y, 8);
	EXPECT_EQ(cell[0].radius, 9);
	EXPECT_EQ(cell[0].attackRadius, 10);
	EXPECT_EQ(cell[0].node, &nodes[0]);
	EXPECT_FALSE(ComputeCellRefresh(cell, &nodes[3], 0, 0, 0, 0));
}

TEST(ComputeBoxMissesLine, MirrorsPitCheckLinesBoxTest)
{
	const LineEntry l = { 0, 10, 0, 10, nullptr };
	EXPECT_FALSE(ComputeBoxMissesLine(l, 5, 15, 5, 15));
	EXPECT_TRUE(ComputeBoxMissesLine(l, -10, 0, 0, 10));   // touching the left edge
	EXPECT_TRUE(ComputeBoxMissesLine(l, 10, 20, 0, 10));   // touching the right edge
	EXPECT_TRUE(ComputeBoxMissesLine(l, 0, 10, -10, 0));
	EXPECT_TRUE(ComputeBoxMissesLine(l, 0, 10, 10, 20));
}
}

namespace {

const int64_t U = 65536;	// [Doruk-Aksoy] one map unit, raw

// [Doruk-Aksoy] A line from (x1, y1) to (x2, y2) in map units.
SightLineEntry Seg(int64_t x1, int64_t y1, int64_t x2, int64_t y2)
{
	const SightLineEntry e = { (int32_t)(x1 * U), (int32_t)(y1 * U), (int32_t)(x2 * U), (int32_t)(y2 * U), 0 };
	return e;
}

} // namespace

// [Doruk-Aksoy] A line across the trace's middle is crossed, whichever way either runs.
TEST(ComputeSightLineCrossed, LineAcrossTheTraceIsCrossed)
{
	EXPECT_TRUE(ComputeSightLineCrossed(Seg(50, -10, 50, 10), 0, 0, 100 * U, 0));
	EXPECT_TRUE(ComputeSightLineCrossed(Seg(50, 10, 50, -10), 0, 0, 100 * U, 0));
	EXPECT_TRUE(ComputeSightLineCrossed(Seg(50, -10, 50, 10), 100 * U, 0, -100 * U, 0));
}

// [Doruk-Aksoy] A line wholly on one side of the trace fails the first test.
TEST(ComputeSightLineCrossed, LineOnOneSideIsNot)
{
	EXPECT_FALSE(ComputeSightLineCrossed(Seg(10, 5, 90, 20), 0, 0, 100 * U, 0));
	EXPECT_FALSE(ComputeSightLineCrossed(Seg(10, -5, 90, -20), 0, 0, 100 * U, 0));
}

// [Doruk-Aksoy] A line across the trace's infinite extension but past its end fails the second test.
TEST(ComputeSightLineCrossed, LinePastTheTracesEndIsNot)
{
	EXPECT_FALSE(ComputeSightLineCrossed(Seg(150, -10, 150, 10), 0, 0, 100 * U, 0));
	EXPECT_FALSE(ComputeSightLineCrossed(Seg(-50, -10, -50, 10), 0, 0, 100 * U, 0));
}

// [Doruk-Aksoy] An end exactly on the trace falls on side 0, as P_PointOnDivlineSide's strict test puts it.
TEST(ComputeSightLineCrossed, EndOnTheTraceFallsOnSideZero)
{
	EXPECT_TRUE(ComputeSightLineCrossed(Seg(50, 0, 50, 10), 0, 0, 100 * U, 0));
	EXPECT_FALSE(ComputeSightLineCrossed(Seg(50, 0, 50, -10), 0, 0, 100 * U, 0));
}

// [Doruk-Aksoy] A trace far past 32 bits takes DMulScale's wide path and still agrees.
TEST(ComputeSightLineCrossed, FarTraceTakesTheWidePath)
{
	const int64_t far = (int64_t)1 << 40;
	EXPECT_TRUE(ComputeSightLineCrossed(Seg(50, -10, 50, 10), 0, 0, far, 0));
	EXPECT_FALSE(ComputeSightLineCrossed(Seg(50, -10, 50, 10), far, 0, far, 0));
}

// [Doruk-Aksoy] Only ends within 32 bits whose difference is the line's dx and dy are stored.
TEST(ComputeSightLineFits, EndsAndDeltas)
{
	const int64_t big = (int64_t)1 << 31;
	EXPECT_TRUE(ComputeSightLineFits(-5 * U, 2 * U, 7 * U, 9 * U, 12 * U, 7 * U));
	EXPECT_FALSE(ComputeSightLineFits(big, 0, 0, 0, -big, 0));
	EXPECT_FALSE(ComputeSightLineFits(0, big, 0, 0, 0, -big));
	EXPECT_FALSE(ComputeSightLineFits(0, 0, big, 0, big, 0));
	EXPECT_FALSE(ComputeSightLineFits(0, 0, 0, big, 0, big));
	EXPECT_FALSE(ComputeSightLineFits(0, 0, 4 * U, 0, 5 * U, 0));
	EXPECT_FALSE(ComputeSightLineFits(0, 0, 0, 4 * U, 0, 5 * U));
}
