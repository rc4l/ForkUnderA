// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/surfaces/computation/wallbands_compute.h"

using namespace zx::surfaces;

namespace {

// A wall from `bottom` to `top`, level at both ends -- which is every wall in a sector without
// sloped planes, and so the shape almost every one of these cases really has.
struct Wall
{
	float top[2], bottom[2];
	Wall(float b, float t) { top[0] = top[1] = t; bottom[0] = bottom[1] = b; }
};

} // namespace

// A sector with no 3D floors has one light and the wall is one piece.
TEST(WallBands, OneLightIsOneBand)
{
	Wall w(0.f, 128.f);
	WallBand out[8];
	const float none[1][2] = { { 0.f, 0.f } };
	const int n = ComputeWallBands(w.top, w.bottom, none, 1, out, 8);
	ASSERT_EQ(1, n);
	EXPECT_FLOAT_EQ(128.f, out[0].ztop[0]);
	EXPECT_FLOAT_EQ(0.f, out[0].zbottom[0]);
	EXPECT_EQ(0, out[0].lightIndex);
}

// [rc4l] One 3D floor across the middle of a wall: two pieces, two lights.
//
// The upper piece keeps the wall's own light -- SplitWall puts the uppermost section with PutWall,
// not Put3DWall, and says so: "uppermost section does not alter light at all".
TEST(WallBands, AFloorAcrossTheMiddleMakesTwoBands)
{
	Wall w(0.f, 128.f);
	const float bands[2][2] = { { 64.f, 64.f }, { 0.f, 0.f } };
	WallBand out[8];
	const int n = ComputeWallBands(w.top, w.bottom, bands, 2, out, 8);
	ASSERT_EQ(2, n);
	EXPECT_FLOAT_EQ(128.f, out[0].ztop[0]);
	EXPECT_FLOAT_EQ(64.f, out[0].zbottom[0]);
	EXPECT_EQ(0, out[0].lightIndex);
	EXPECT_TRUE(out[0].ownLight);

	EXPECT_FLOAT_EQ(64.f, out[1].ztop[0]);
	EXPECT_FLOAT_EQ(0.f, out[1].zbottom[0]);
	EXPECT_EQ(1, out[1].lightIndex);
	EXPECT_FALSE(out[1].ownLight);
}

TEST(WallBands, TwoFloorsMakeThreeBands)
{
	Wall w(0.f, 192.f);
	const float bands[3][2] = { { 128.f, 128.f }, { 64.f, 64.f }, { 0.f, 0.f } };
	WallBand out[8];
	const int n = ComputeWallBands(w.top, w.bottom, bands, 3, out, 8);
	ASSERT_EQ(3, n);
	EXPECT_FLOAT_EQ(192.f, out[0].ztop[0]); EXPECT_FLOAT_EQ(128.f, out[0].zbottom[0]);
	EXPECT_FLOAT_EQ(128.f, out[1].ztop[0]); EXPECT_FLOAT_EQ(64.f,  out[1].zbottom[0]);
	EXPECT_FLOAT_EQ(64.f,  out[2].ztop[0]); EXPECT_FLOAT_EQ(0.f,   out[2].zbottom[0]);
	EXPECT_EQ(0, out[0].lightIndex);
	EXPECT_EQ(1, out[1].lightIndex);
	EXPECT_EQ(2, out[2].lightIndex);
}

// A light whose bottom is above the whole wall lights nothing on it and must not produce a piece.
TEST(WallBands, ALightEntirelyAboveTheWallIsSkipped)
{
	Wall w(0.f, 64.f);
	const float bands[2][2] = { { 200.f, 200.f }, { 0.f, 0.f } };
	WallBand out[8];
	const int n = ComputeWallBands(w.top, w.bottom, bands, 2, out, 8);
	ASSERT_EQ(1, n);
	EXPECT_FLOAT_EQ(64.f, out[0].ztop[0]);
	EXPECT_FLOAT_EQ(0.f, out[0].zbottom[0]);
	EXPECT_EQ(1, out[0].lightIndex);   // the band it actually falls in, not the one above it
}

// ...and one whose bottom is at or below the wall covers the rest of it in a single piece.
TEST(WallBands, ALightBelowTheWallTakesAllOfIt)
{
	Wall w(100.f, 200.f);
	const float bands[2][2] = { { 50.f, 50.f }, { 0.f, 0.f } };
	WallBand out[8];
	const int n = ComputeWallBands(w.top, w.bottom, bands, 2, out, 8);
	ASSERT_EQ(1, n);
	EXPECT_FLOAT_EQ(200.f, out[0].ztop[0]);
	EXPECT_FLOAT_EQ(100.f, out[0].zbottom[0]);
	EXPECT_EQ(0, out[0].lightIndex);
	EXPECT_TRUE(out[0].ownLight);
}

// [rc4l] A SLOPED band boundary keeps its slope: each end is cut at its own height.
//
// This is the case the single-ended version of the question cannot answer, and the reason the
// boundary is passed as a pair rather than a number.
TEST(WallBands, ASlopedBoundaryCutsEachEndAtItsOwnHeight)
{
	Wall w(0.f, 128.f);
	const float bands[2][2] = { { 32.f, 96.f }, { 0.f, 0.f } };
	WallBand out[8];
	const int n = ComputeWallBands(w.top, w.bottom, bands, 2, out, 8);
	ASSERT_EQ(2, n);
	EXPECT_FLOAT_EQ(32.f, out[0].zbottom[0]);
	EXPECT_FLOAT_EQ(96.f, out[0].zbottom[1]);
	EXPECT_FLOAT_EQ(32.f, out[1].ztop[0]);
	EXPECT_FLOAT_EQ(96.f, out[1].ztop[1]);
}

// A boundary exactly on the wall's top is not a cut: it would make a piece of no height.
TEST(WallBands, ABoundaryOnTheTopEdgeMakesNoEmptyPiece)
{
	Wall w(0.f, 128.f);
	const float bands[2][2] = { { 128.f, 128.f }, { 0.f, 0.f } };
	WallBand out[8];
	const int n = ComputeWallBands(w.top, w.bottom, bands, 2, out, 8);
	ASSERT_EQ(1, n);
	EXPECT_FLOAT_EQ(128.f, out[0].ztop[0]);
	EXPECT_FLOAT_EQ(0.f, out[0].zbottom[0]);
	EXPECT_EQ(1, out[0].lightIndex);
}

// The caller asks this first and keeps its single-piece path when the answer is no, which is most
// walls even in a sector that has 3D floors.
TEST(WallBands, CrossingIsAnsweredWithoutSplitting)
{
	Wall inside(0.f, 64.f);
	Wall crossing(0.f, 128.f);
	const float bands[2][2] = { { 96.f, 96.f }, { 0.f, 0.f } };
	EXPECT_FALSE(WallCrossesABand(inside.top, inside.bottom, bands, 2));
	EXPECT_TRUE(WallCrossesABand(crossing.top, crossing.bottom, bands, 2));
}

// The output is bounded, because a pathological light list must not write past the caller's array.
TEST(WallBands, TheOutputIsBounded)
{
	Wall w(0.f, 512.f);
	float bands[9][2];
	for (int i = 0; i < 9; i++) { bands[i][0] = bands[i][1] = 448.f - i * 56.f; }
	WallBand out[3];
	const int n = ComputeWallBands(w.top, w.bottom, bands, 9, out, 3);
	EXPECT_LE(n, 3);
}
