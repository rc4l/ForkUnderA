// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/server-browser/computation/pillflow_compute.h"

using namespace zx;

namespace
{

std::vector<int> Widths(int a, int b = -1, int c = -1, int d = -1)
{
	std::vector<int> w;
	w.push_back(a);
	if (b >= 0) w.push_back(b);
	if (c >= 0) w.push_back(c);
	if (d >= 0) w.push_back(d);
	return w;
}

} // namespace

TEST(FlowPills, PutsThemSideBySideWhileTheyFit)
{
	const std::vector<PillPlace> p = FlowPills(Widths(30, 30, 30), 100, 5);

	ASSERT_EQ(3u, p.size());
	EXPECT_EQ(0, p[0].row);
	EXPECT_EQ(0, p[1].row);
	EXPECT_EQ(0, p[2].row);

	EXPECT_EQ(0, p[0].x);
	EXPECT_EQ(35, p[1].x);
	EXPECT_EQ(70, p[2].x);
}

TEST(FlowPills, WrapsWhenTheNextOneWouldRunPastTheEdge)
{
	const std::vector<PillPlace> p = FlowPills(Widths(60, 60), 100, 5);

	ASSERT_EQ(2u, p.size());
	EXPECT_EQ(0, p[0].row);
	EXPECT_EQ(1, p[1].row);
	EXPECT_EQ(0, p[1].x) << "a wrapped pill starts at the left edge";
}

TEST(FlowPills, NeverWrapsTheFirstPillOfARow)
{
	// One wider than the whole box. Wrapping it would leave an empty row above it and still not
	// fit, so it takes the row it is on and the caller clips the text.
	const std::vector<PillPlace> p = FlowPills(Widths(500, 20), 100, 5);

	ASSERT_EQ(2u, p.size());
	EXPECT_EQ(0, p[0].row);
	EXPECT_EQ(0, p[0].x);
	EXPECT_EQ(1, p[1].row) << "the one after it wraps, because the wide one filled that row";
}

TEST(FlowPills, TheGapSitsBetweenNeighboursAndNotAtTheEnds)
{
	const std::vector<PillPlace> p = FlowPills(Widths(20, 20), 100, 7);

	ASSERT_EQ(2u, p.size());
	EXPECT_EQ(0, p[0].x);
	EXPECT_EQ(27, p[1].x);
}

TEST(FlowPills, SurvivesNonsenseWithoutProducingNonsense)
{
	// A zero-width box and a zero-width pill both come back as something a caller can draw.
	const std::vector<PillPlace> zeroBox = FlowPills(Widths(10), 0, 4);
	ASSERT_EQ(1u, zeroBox.size());
	EXPECT_EQ(0, zeroBox[0].row);

	const std::vector<PillPlace> zeroPill = FlowPills(Widths(0), 100, 4);
	ASSERT_EQ(1u, zeroPill.size());
	EXPECT_GT(zeroPill[0].width, 0);

	const std::vector<PillPlace> negGap = FlowPills(Widths(10, 10), 100, -5);
	ASSERT_EQ(2u, negGap.size());
	EXPECT_GE(negGap[1].x, negGap[0].x + negGap[0].width);
}

TEST(FlowPills, AnEmptyListIsAnEmptyLayout)
{
	const std::vector<int> none;
	EXPECT_TRUE(FlowPills(none, 100, 5).empty());
}

// ---------------------------------------------------------------- height

TEST(PillFlowRowCount, CountsTheRowsAndNotTheItems)
{
	EXPECT_EQ(1, PillFlowRowCount(FlowPills(Widths(30, 30), 100, 5)));
	EXPECT_EQ(2, PillFlowRowCount(FlowPills(Widths(60, 60), 100, 5)));
	EXPECT_EQ(0, PillFlowRowCount(std::vector<PillPlace>()));
}

TEST(PillFlowRowCount, IsWhatSaysAListNoLongerFits)
{
	// The reason this is computed rather than eyeballed: the height depends on the labels, which
	// depend on the player's files, so nobody can know at authoring time whether it overflows.
	std::vector<int> many;
	for (int i = 0; i < 25; ++i)
		many.push_back(45);

	const std::vector<PillPlace> p = FlowPills(many, 200, 5);

	EXPECT_GT(PillFlowRowCount(p), 5) << "twenty-five of these do not fit in a five-row box";
}

// ---------------------------------------------------------------- hit testing

TEST(PillFlowHitTest, FindsThePillUnderThePoint)
{
	const std::vector<PillPlace> p = FlowPills(Widths(30, 30), 100, 5);

	EXPECT_EQ(0, PillFlowHitTest(p, 20, 5, 5));
	EXPECT_EQ(1, PillFlowHitTest(p, 20, 40, 5));
}

TEST(PillFlowHitTest, FindsNothingInTheGapsOrOffTheEnd)
{
	const std::vector<PillPlace> p = FlowPills(Widths(30, 30), 100, 5);

	EXPECT_EQ(-1, PillFlowHitTest(p, 20, 32, 5)) << "between two pills";
	EXPECT_EQ(-1, PillFlowHitTest(p, 20, 95, 5)) << "past the last one";
	EXPECT_EQ(-1, PillFlowHitTest(p, 20, 5, -3)) << "above the first row";
	EXPECT_EQ(-1, PillFlowHitTest(p, 20, 5, 999)) << "below the last row";
}

TEST(PillFlowHitTest, ReadsTheSecondRowAsTheSecondRow)
{
	const std::vector<PillPlace> p = FlowPills(Widths(60, 60), 100, 5);

	EXPECT_EQ(0, PillFlowHitTest(p, 20, 5, 5));
	EXPECT_EQ(1, PillFlowHitTest(p, 20, 5, 25));
}

TEST(PillFlowHitTest, ARowHeightOfNothingHitsNothing)
{
	const std::vector<PillPlace> p = FlowPills(Widths(30), 100, 5);
	EXPECT_EQ(-1, PillFlowHitTest(p, 0, 5, 5));
}

TEST(PillPlace, DefaultsToTheOrigin)
{
	// The default constructor is what lets a place be filled in as the row is laid out; starting
	// anywhere but the origin would put a pill on screen before anything had decided where.
	const PillPlace place;

	EXPECT_EQ(0, place.x);
	EXPECT_EQ(0, place.row);
	EXPECT_EQ(0, place.width);
}
