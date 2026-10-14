// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/levelmesh/computation/seamless_compute.h"

using namespace zx::levelmesh;

namespace {

// [rc4l] A wall from (0, 0) to (128, 0), 0 to 128 high, its texture 0..1 across and 0 (top) to 1 (bottom) down.
void Corners(SeamVertex c[4])
{
	const SeamVertex k[4] = { { 0, 0, 0, 0, 1 }, { 0, 0, 128, 0, 0 }, { 128, 0, 128, 1, 0 }, { 128, 0, 0, 1, 1 } };
	for (int i = 0; i < 4; i++) c[i] = k[i];
}

SeamSplits NoSplits()
{
	SeamSplits s = {};
	s.fracLeft = 0.f; s.fracRight = 1.f;
	return s;
}

} // namespace

// [rc4l] gl_RecalcVertexHeights: sorted, each height once, and nothing kept for a vertex two planes meet at.
TEST(Seamless, AVertexKeepsItsDistinctHeightsAscendingOnlyPastTwo)
{
	const float heights[] = { 128, 0, 64, 0, 128, 32 };
	float out[6];
	ASSERT_EQ(4, ComputeVertexHeights(heights, 6, out));
	EXPECT_EQ(0.f, out[0]); EXPECT_EQ(32.f, out[1]); EXPECT_EQ(64.f, out[2]); EXPECT_EQ(128.f, out[3]);
	const float two[] = { 128, 0, 128, 0 };
	EXPECT_EQ(0, ComputeVertexHeights(two, 4, out));
}

// [rc4l] A blended wall and a middle with one sector on both sides draw unsplit.
TEST(Seamless, BlendedWallsAndSameSectorMiddlesTakeNoSeams)
{
	EXPECT_TRUE(ComputeWallTakesSeams(false, false));
	EXPECT_FALSE(ComputeWallTakesSeams(true, false));
	EXPECT_FALSE(ComputeWallTakesSeams(false, true));
}

// [rc4l] A lone band splits both edges; across three, the top splits only its top, the bottom its bottom, the middle neither.
TEST(Seamless, ALightBoundarySplitsNeitherPieceAtIt)
{
	bool upper, lower;
	ComputeBandSeams(0, 1, upper, lower);
	EXPECT_TRUE(upper); EXPECT_TRUE(lower);
	ComputeBandSeams(0, 3, upper, lower);
	EXPECT_TRUE(upper); EXPECT_FALSE(lower);
	ComputeBandSeams(1, 3, upper, lower);
	EXPECT_FALSE(upper); EXPECT_FALSE(lower);
	ComputeBandSeams(2, 3, upper, lower);
	EXPECT_FALSE(upper); EXPECT_TRUE(lower);
}

TEST(Seamless, WithNothingToSplitTheFanIsItsFourCorners)
{
	SeamVertex c[4], out[8];
	Corners(c);
	ASSERT_EQ(4, ComputeSeamlessFan(c, NoSplits(), out, 8));
	for (int i = 0; i < 4; i++) { EXPECT_EQ(c[i].z, out[i].z); EXPECT_EQ(c[i].u, out[i].u); }
}

// [rc4l] The left edge takes the heights strictly inside it bottom up, the right edge top down, with v following the height.
TEST(Seamless, TheEndsSplitAtTheirVerticesHeights)
{
	SeamVertex c[4], out[16];
	Corners(c);
	const float left[] = { 0, 32, 96, 128 }, right[] = { -16, 0, 64, 128, 160 };
	SeamSplits s = NoSplits();
	s.leftHeights = left; s.leftCount = 4; s.rightHeights = right; s.rightCount = 5;
	s.left = s.right = true;
	ASSERT_EQ(7, ComputeSeamlessFan(c, s, out, 16));
	EXPECT_EQ(32.f, out[1].z); EXPECT_EQ(0.75f, out[1].v); EXPECT_EQ(0.f, out[1].x);
	EXPECT_EQ(96.f, out[2].z); EXPECT_EQ(0.25f, out[2].v);
	EXPECT_EQ(128.f, out[3].z);
	EXPECT_EQ(128.f, out[4].z);
	EXPECT_EQ(64.f, out[5].z); EXPECT_EQ(0.5f, out[5].v); EXPECT_EQ(128.f, out[5].x);
	EXPECT_EQ(0.f, out[6].z);
}

// [rc4l] GL's right-edge loop stops above index 0, so the lowest height never splits it even when it is inside the edge.
TEST(Seamless, TheRightEdgeNeverTakesItsLowestHeight)
{
	SeamVertex c[4], out[8];
	Corners(c);
	const float right[] = { 16, 300 };
	SeamSplits s = NoSplits();
	s.rightHeights = right; s.rightCount = 2; s.right = true;
	EXPECT_EQ(4, ComputeSeamlessFan(c, s, out, 8));
}

// [rc4l] The top runs left to right and the bottom right to left, each through the seg ends inside the wall's part of the sidedef.
TEST(Seamless, TheTopAndBottomSplitWhereTheSidedefsSegsEnd)
{
	SeamVertex c[4], out[16];
	Corners(c);
	c[2].z = 192.f;   // [rc4l] a sloped top: 128 at the left, 192 at the right
	const float sx[] = { 16, 64, 112 }, sy[] = { 0, 0, 0 }, sf[] = { 0.125f, 0.5f, 0.875f };
	SeamSplits s = NoSplits();
	s.segX = sx; s.segY = sy; s.segFrac = sf; s.segCount = 3;
	s.upper = s.lower = true;
	ASSERT_EQ(10, ComputeSeamlessFan(c, s, out, 16));
	EXPECT_EQ(16.f, out[2].x); EXPECT_EQ(136.f, out[2].z); EXPECT_EQ(0.125f, out[2].u);
	EXPECT_EQ(64.f, out[3].x); EXPECT_EQ(160.f, out[3].z);
	EXPECT_EQ(112.f, out[4].x);
	EXPECT_EQ(112.f, out[7].x); EXPECT_EQ(0.f, out[7].z); EXPECT_EQ(0.875f, out[7].u); EXPECT_EQ(1.f, out[7].v);
	EXPECT_EQ(16.f, out[9].x);
}

// [rc4l] A wall over part of its sidedef takes only the seg ends inside that part, measured from its own left end.
TEST(Seamless, AWallOverPartOfItsSidedefTakesOnlyTheEndsInsideIt)
{
	SeamVertex c[4], out[16];
	Corners(c);
	const float sx[] = { 16, 64, 112 }, sy[] = { 0, 0, 0 }, sf[] = { 0.125f, 0.5f, 0.875f };
	SeamSplits s = NoSplits();
	s.segX = sx; s.segY = sy; s.segFrac = sf; s.segCount = 3;
	s.fracLeft = 0.25f; s.fracRight = 0.75f;
	s.upper = s.lower = true;
	ASSERT_EQ(6, ComputeSeamlessFan(c, s, out, 16));
	EXPECT_EQ(64.f, out[2].x); EXPECT_EQ(0.5f, out[2].u);
	EXPECT_EQ(64.f, out[5].x);
}

// [rc4l] A wall with no height has no edge for any height to fall inside, and an overrun is no fan at all.
TEST(Seamless, AFlatWallTakesNoSplitsAndAnOverrunGivesNoFan)
{
	SeamVertex c[4], out[8];
	Corners(c);
	c[1].z = c[0].z = 0.f;
	c[2].z = c[3].z = 0.f;
	const float left[] = { -8, 0, 8 };
	SeamSplits s = NoSplits();
	s.leftHeights = left; s.leftCount = 3; s.rightHeights = left; s.rightCount = 3; s.left = s.right = true;
	ASSERT_EQ(4, ComputeSeamlessFan(c, s, out, 8));
	Corners(c);
	const float many[] = { 0, 10, 20, 30, 40, 50, 60, 70, 80, 128 };
	s.leftHeights = many; s.leftCount = 10;
	EXPECT_EQ(0, ComputeSeamlessFan(c, s, out, 8));
}
