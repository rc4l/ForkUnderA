// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-client/computation/netgraph_compute.h"

#include <gtest/gtest.h>

using namespace zx::netclient;

TEST(NetGraph, TheGraphIsAMinuteWide)
{
	EXPECT_EQ(NETGRAPH_WIDTH, 472);
	EXPECT_EQ(NETGRAPH_HEIGHT, 120);
}

TEST(NetGraph, LargestValueOfNoneIsZero)
{
	EXPECT_EQ(ComputeLargestValue(nullptr, 0), 0u);
	const unsigned int values[] = { 3, 9, 0, 7 };
	EXPECT_EQ(ComputeLargestValue(values, 4), 9u);
	EXPECT_EQ(ComputeLargestValue(values, 1), 3u);
}

TEST(NetGraph, ScaleMaxKeepsItsDefaultUntilALineOutgrowsIt)
{
	EXPECT_EQ(ComputeNetGraphScaleMax(6000, 2000, { 1000, 3999 }), 6000u);
	EXPECT_EQ(ComputeNetGraphScaleMax(6000, 2000, { 1000, 4001 }), 6001u);
	EXPECT_EQ(ComputeNetGraphScaleMax(10, 2, {}), 10u);
}

TEST(NetGraph, ScaleMaxIsNeverZero)
{
	EXPECT_EQ(ComputeNetGraphScaleMax(0, 0, { 0 }), 1u);
}

TEST(NetGraph, ScaleTopIsInTheDividersUnits)
{
	EXPECT_EQ(ComputeNetGraphScaleTop(6500, 1000), 6u);
	EXPECT_EQ(ComputeNetGraphScaleTop(12, 1), 12u);
	EXPECT_EQ(ComputeNetGraphScaleTop(12, 0), 12u);
}

TEST(NetGraph, PointsRunLeftToRightOldestFirst)
{
	EXPECT_EQ(ComputeNetGraphPointX(5, 0), 5);
	EXPECT_EQ(ComputeNetGraphPointX(5, NETGRAPH_SECONDS - 1), 5 + NETGRAPH_WIDTH);
}

TEST(NetGraph, PointsRiseWithTheirValue)
{
	EXPECT_EQ(ComputeNetGraphPointY(0, 500, 6000), 500);
	EXPECT_EQ(ComputeNetGraphPointY(3000, 500, 6000), 440);
	EXPECT_EQ(ComputeNetGraphPointY(6000, 500, 6000), 380);
}

TEST(NetGraph, PointsOfHugeValuesDoNotWrap)
{
	EXPECT_EQ(ComputeNetGraphPointY(40000000u, 500, 40002000u), 381);
}

TEST(NetGraph, PointsStayInsideTheGraph)
{
	EXPECT_EQ(ComputeNetGraphPointY(9000, 500, 6000), 380);
	EXPECT_EQ(ComputeNetGraphPointY(5, 500, 0), 380);
}

TEST(NetGraph, VerticalGridLinesMoveLeftEachSecond)
{
	EXPECT_EQ(ComputeNetGraphVerticalGridLines(0, 0), (std::vector<int>{ 80, 160, 240, 320, 400 }));
	EXPECT_EQ(ComputeNetGraphVerticalGridLines(0, 1), (std::vector<int>{ 72, 152, 232, 312, 392 }));
	EXPECT_EQ(ComputeNetGraphVerticalGridLines(5, 19), (std::vector<int>{ 13, 93, 173, 253, 333, 413 }));
}

TEST(NetGraph, VerticalGridLinesBeforeTheFirstSecondStandStill)
{
	EXPECT_EQ(ComputeNetGraphVerticalGridLines(0, -3), ComputeNetGraphVerticalGridLines(0, 0));
}

TEST(NetGraph, HorizontalGridLinesQuarterTheHeight)
{
	EXPECT_EQ(ComputeNetGraphHorizontalGridLines(100), (std::vector<int>{ 130, 160, 190 }));
}

TEST(NetGraph, GraphsStackAboveTheStatText)
{
	EXPECT_EQ(ComputeNetGraphTopY(700, 0, 8), 576);
	EXPECT_EQ(ComputeNetGraphTopY(700, 1, 8), 576 - 136);
}

TEST(NetGraph, LegendsFollowEachOtherAlongARow)
{
	const auto places = ComputeNetGraphLegend(5, 100, { { true, 12 }, { true, 20 } }, 8);
	ASSERT_EQ(places.size(), 2u);
	EXPECT_TRUE(places[0].drawn);
	EXPECT_EQ(places[0].x, 15);
	EXPECT_EQ(places[0].y, 110);
	EXPECT_TRUE(places[1].drawn);
	EXPECT_EQ(places[1].x, 15 + 16 + 10 + 12 + 10);
	EXPECT_EQ(places[1].y, 110);
}

TEST(NetGraph, LegendsWithoutANameAreLeftOut)
{
	const auto places = ComputeNetGraphLegend(0, 0, { { false, 12 }, { true, 12 } }, 8);
	EXPECT_FALSE(places[0].drawn);
	EXPECT_TRUE(places[1].drawn);
	EXPECT_EQ(places[1].x, 10);
}

TEST(NetGraph, LegendsWrapToANewRow)
{
	// [rc4l] Each takes 16 + 10 + 150 + 10 = 186 pixels, so the third passes the right edge less its spacing.
	const auto places = ComputeNetGraphLegend(0, 0, { { true, 150 }, { true, 150 }, { true, 150 } }, 8);
	EXPECT_EQ(places[1].x, 10 + 186);
	EXPECT_EQ(places[2].x, 10);
	EXPECT_EQ(places[2].y, 10 + 10);
}

TEST(NetGraph, ALegendWiderThanTheGraphIsLeftOut)
{
	const auto places = ComputeNetGraphLegend(0, 0, { { true, 400 }, { true, 440 }, { true, 12 } }, 8);
	EXPECT_TRUE(places[0].drawn);
	EXPECT_FALSE(places[1].drawn);
	EXPECT_TRUE(places[2].drawn);
	EXPECT_EQ(places[2].y, 20);
}

TEST(NetGraph, LegendsStopWhereNoRowIsLeft)
{
	// [rc4l] Rows 52 pixels apart: the second at 62, the third at 114 past the bottom less its spacing.
	const auto places = ComputeNetGraphLegend(0, 0, { { true, 400 }, { true, 400 }, { true, 400 } }, 50);
	EXPECT_TRUE(places[0].drawn);
	EXPECT_TRUE(places[1].drawn);
	EXPECT_EQ(places[1].y, 62);
	EXPECT_FALSE(places[2].drawn);
}
