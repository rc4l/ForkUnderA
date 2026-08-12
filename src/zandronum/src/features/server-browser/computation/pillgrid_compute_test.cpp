// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/pillgrid_compute.h"

using zx::LayoutWadList;
using zx::MovePillVertically;
using zx::PillMove;
using zx::WadListLayout;

namespace
{

const int kGap = 4;

// Six pills of the same width, wrapping three to a line: a tidy 3x2 grid to reason about.
std::vector<int> Even()
{
	return std::vector<int>(6, 30);
}

WadListLayout EvenGrid()
{
	// Room for three 30s and the two gaps between them, and not a pixel more.
	return LayoutWadList(Even(), kGap, 0, 30 * 3 + kGap * 2, 0);
}

} // namespace

TEST(PillGrid, ADownMoveLandsOnTheLineBelow)
{
	const WadListLayout grid = EvenGrid();
	ASSERT_EQ(2u, grid.lines.size());

	const PillMove at = MovePillVertically(grid, Even(), kGap, 1, 1);

	EXPECT_FALSE(at.leaves);
	EXPECT_EQ(4, at.index) << "the middle of the first line goes to the middle of the second";
}

TEST(PillGrid, AnUpMoveLandsOnTheLineAbove)
{
	const PillMove at = MovePillVertically(EvenGrid(), Even(), kGap, 5, -1);

	EXPECT_FALSE(at.leaves);
	EXPECT_EQ(2, at.index);
}

TEST(PillGrid, UpOffTheFirstLineLeavesTheAxis)
{
	// Which is what lets the control above the axis still be reachable.
	EXPECT_TRUE(MovePillVertically(EvenGrid(), Even(), kGap, 0, -1).leaves);
	EXPECT_TRUE(MovePillVertically(EvenGrid(), Even(), kGap, 2, -1).leaves);
}

TEST(PillGrid, DownOffTheLastLineLeavesTheAxis)
{
	EXPECT_TRUE(MovePillVertically(EvenGrid(), Even(), kGap, 3, 1).leaves);
	EXPECT_TRUE(MovePillVertically(EvenGrid(), Even(), kGap, 5, 1).leaves);
}

TEST(PillGrid, AnAxisOnOneLineHasNowhereVerticalToGo)
{
	// The ordinary case: two or three options that fit across. Up and down belong to the region.
	std::vector<int> two(2, 30);
	const WadListLayout one = LayoutWadList(two, kGap, 0, 400, 0);

	ASSERT_EQ(1u, one.lines.size());
	EXPECT_TRUE(MovePillVertically(one, two, kGap, 0, 1).leaves);
	EXPECT_TRUE(MovePillVertically(one, two, kGap, 1, -1).leaves);
}

TEST(PillGrid, TheNearestPillIsFoundByCentreNotByPosition)
{
	// [rc4l] The reason this is not "the nth pill of the next line". A wide first pill pushes the
	// whole line right, so index-matching would jump the marker sideways on a key pointing down.
	//
	//   line 0:  [ 0: 200 ................................ ]
	//   line 1:  [ 1: 20 ][ 2: 20 ][ 3: 20 ][ 4: 20 ]
	std::vector<int> widths;
	widths.push_back(200);
	widths.push_back(20);
	widths.push_back(20);
	widths.push_back(20);
	widths.push_back(20);

	const WadListLayout grid = LayoutWadList(widths, kGap, 0, 200, 0);
	ASSERT_EQ(2u, grid.lines.size());

	// The wide pill's centre is at 100; the second line's pills sit at 10, 34, 58 and 82, so the
	// last of them is nearest.
	const PillMove at = MovePillVertically(grid, widths, kGap, 0, 1);

	EXPECT_FALSE(at.leaves);
	EXPECT_EQ(4, at.index);
}

TEST(PillGrid, AShorterLineBelowStillCatchesTheMove)
{
	// The last line is usually short. Whatever is on it must still be reachable from above, or the
	// tail of a wrapped axis becomes a place only left and right can get to.
	std::vector<int> widths(5, 30);
	const WadListLayout grid = LayoutWadList(widths, kGap, 0, 30 * 3 + kGap * 2, 0);

	ASSERT_EQ(2u, grid.lines.size());

	const PillMove at = MovePillVertically(grid, widths, kGap, 2, 1);

	EXPECT_FALSE(at.leaves);
	EXPECT_EQ(4, at.index) << "the rightmost of a short line is the nearest thing under a right-hand pill";
}

TEST(PillGrid, NonsenseAsksLeaveRatherThanGuess)
{
	const WadListLayout grid = EvenGrid();

	EXPECT_TRUE(MovePillVertically(grid, Even(), kGap, 0, 0).leaves) << "no direction";
	EXPECT_TRUE(MovePillVertically(grid, Even(), kGap, -1, 1).leaves);
	EXPECT_TRUE(MovePillVertically(grid, Even(), kGap, 99, 1).leaves) << "not in the layout";
	EXPECT_TRUE(MovePillVertically(WadListLayout(), Even(), kGap, 0, 1).leaves) << "nothing laid out";
}
