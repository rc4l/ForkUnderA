// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/modelreach/computation/modelreach_compute.h"

#include <gtest/gtest.h>

using namespace zx::modelreach;

namespace {

// [Doruk-Aksoy] A frame at unit scale, no offset, stretch or spin.
FramePlacement Plain()
{
	FramePlacement p = {};
	p.scale[0] = p.scale[1] = p.scale[2] = 1.f;
	p.stretch = 1.f;
	return p;
}

ModelReach Empty()
{
	ModelReach r = {};
	return r;
}

} // namespace

// [Doruk-Aksoy] Vertices five floats apart as FModelVertex lays them out, (x, up, y) then u and v.
TEST(ModelReachVertices, FarthestInFullAndAcrossTheFlatAxes)
{
	const float verts[] = {
		3.f, 4.f, 0.f, 9.f, 9.f,     // [Doruk-Aksoy] 5 away, 3 across
		0.f, 1.f, -4.f, 9.f, 9.f,    // [Doruk-Aksoy] about 4.1 away, 4 across
	};
	const VertexReach r = ComputeVertexReach(verts, 2, 5);
	EXPECT_FLOAT_EQ(5.f, r.full);
	EXPECT_FLOAT_EQ(4.f, r.flat);
}

TEST(ModelReachVertices, NoVerticesReachNothing)
{
	const VertexReach r = ComputeVertexReach(nullptr, 0, 5);
	EXPECT_EQ(0.f, r.full);
	EXPECT_EQ(0.f, r.flat);
}

TEST(ModelReachFrame, PlainFrameReachesAsFarAsItsVertices)
{
	ModelReach r = Empty();
	const VertexReach v = { 10.f, 6.f };
	ComputeAddFrameReach(v, Plain(), r);
	EXPECT_FLOAT_EQ(6.f, r.flatScaled);
	EXPECT_FLOAT_EQ(0.f, r.flatFixed);
	EXPECT_FLOAT_EQ(10.f, r.fullScaled);
	EXPECT_FLOAT_EQ(0.f, r.fullFixed);
}

// [Doruk-Aksoy] Up's scale and stretch leave the flat reach alone; the full one takes the largest axis and the stretch.
TEST(ModelReachFrame, ScaleAndStretchGrowTheirOwnAxes)
{
	ModelReach r = Empty();
	FramePlacement p = Plain();
	p.scale[0] = 2.f; p.scale[1] = 8.f; p.scale[2] = -3.f;
	p.stretch = 1.2f;
	const VertexReach v = { 10.f, 6.f };
	ComputeAddFrameReach(v, p, r);
	EXPECT_FLOAT_EQ(18.f, r.flatScaled);
	EXPECT_FLOAT_EQ(96.f, r.fullScaled);
}

TEST(ModelReachFrame, StretchBelowOneNeverShrinks)
{
	ModelReach r = Empty();
	FramePlacement p = Plain();
	p.stretch = 0.5f;
	const VertexReach v = { 10.f, 6.f };
	ComputeAddFrameReach(v, p, r);
	EXPECT_FLOAT_EQ(10.f, r.fullScaled);
}

TEST(ModelReachFrame, OffsetsAddTheirLength)
{
	ModelReach r = Empty();
	FramePlacement p = Plain();
	p.offset[0] = 3.f; p.offset[1] = 12.f; p.offset[2] = 4.f;
	const VertexReach v = { 10.f, 6.f };
	ComputeAddFrameReach(v, p, r);
	EXPECT_FLOAT_EQ(11.f, r.flatScaled);
	EXPECT_FLOAT_EQ(23.f, r.fullScaled);
}

// [Doruk-Aksoy] A spin about up around a centre off the origin swings the model up to twice the centre's distance, which the actor's scale leaves alone.
TEST(ModelReachFrame, SpinAboutUpAddsTwiceItsCentre)
{
	ModelReach r = Empty();
	FramePlacement p = Plain();
	p.rotating = true;
	p.spinAxis[1] = 1.f;
	p.spinCenter[0] = 3.f; p.spinCenter[1] = 12.f; p.spinCenter[2] = 4.f;
	const VertexReach v = { 10.f, 6.f };
	ComputeAddFrameReach(v, p, r);
	EXPECT_FLOAT_EQ(6.f, r.flatScaled);
	EXPECT_FLOAT_EQ(10.f, r.flatFixed);
	EXPECT_FLOAT_EQ(26.f, r.fullFixed);
}

TEST(ModelReachFrame, SpinAboutAFlatAxisTiltsTheModel)
{
	ModelReach r = Empty();
	FramePlacement p = Plain();
	p.rotating = true;
	p.spinAxis[0] = 1.f;
	const VertexReach v = { 10.f, 6.f };
	ComputeAddFrameReach(v, p, r);
	EXPECT_FLOAT_EQ(10.f, r.flatScaled);
	EXPECT_FLOAT_EQ(0.f, r.flatFixed);
}

TEST(ModelReachFrame, TiltTakesTheFullReachAcross)
{
	ModelReach r = Empty();
	FramePlacement p = Plain();
	p.tilts = true;
	const VertexReach v = { 10.f, 6.f };
	ComputeAddFrameReach(v, p, r);
	EXPECT_FLOAT_EQ(10.f, r.flatScaled);
}

// [Doruk-Aksoy] Each part keeps the largest any frame needed, so the actor's reach does not change as it animates.
TEST(ModelReachFrame, FramesKeepEachPartsLargest)
{
	ModelReach r = Empty();
	FramePlacement spun = Plain();
	spun.rotating = true;
	spun.spinCenter[0] = 5.f;
	const VertexReach small = { 2.f, 1.f };
	const VertexReach big = { 20.f, 12.f };
	ComputeAddFrameReach(small, spun, r);
	ComputeAddFrameReach(big, Plain(), r);
	ComputeAddFrameReach(small, Plain(), r);
	EXPECT_FLOAT_EQ(12.f, r.flatScaled);
	EXPECT_FLOAT_EQ(10.f, r.flatFixed);
	EXPECT_FLOAT_EQ(20.f, r.fullScaled);
	EXPECT_FLOAT_EQ(10.f, r.fullFixed);
}

TEST(ModelReachRenderRadius, NegativeTurnsItOff)
{
	ModelReach r = Empty();
	ComputeApplyRenderRadius(-1, r);
	EXPECT_TRUE(r.off);
}

TEST(ModelReachRenderRadius, PositiveRaisesTheFloorAndNeverLowersIt)
{
	ModelReach r = Empty();
	ComputeApplyRenderRadius(0, r);
	EXPECT_FLOAT_EQ(0.f, r.floor);
	ComputeApplyRenderRadius(300, r);
	EXPECT_FLOAT_EQ(300.f, r.floor);
	ComputeApplyRenderRadius(100, r);
	EXPECT_FLOAT_EQ(300.f, r.floor);
	EXPECT_FALSE(r.off);
}

TEST(ModelReachActor, ScaledPartTakesTheActorsScale)
{
	ModelReach r = Empty();
	r.flatScaled = 10.f; r.flatFixed = 4.f; r.fullScaled = 20.f; r.fullFixed = 6.f;
	float footprint, full;
	ComputeActorReach(r, -2.f, 3.f, footprint, full);
	EXPECT_FLOAT_EQ(24.f, footprint);
	EXPECT_FLOAT_EQ(66.f, full);
	ComputeActorReach(r, 2.f, 0.5f, footprint, full);
	EXPECT_FLOAT_EQ(46.f, full);
}

TEST(ModelReachActor, FloorWinsOverASmallerModel)
{
	ModelReach r = Empty();
	r.flatScaled = 10.f; r.fullScaled = 20.f; r.floor = 256.f;
	float footprint, full;
	ComputeActorReach(r, 1.f, 1.f, footprint, full);
	EXPECT_FLOAT_EQ(256.f, footprint);
	EXPECT_FLOAT_EQ(256.f, full);
}

TEST(ModelReachActor, OffReachesNothing)
{
	ModelReach r = Empty();
	r.flatScaled = 10.f; r.fullScaled = 20.f; r.floor = 256.f; r.off = true;
	float footprint = -1.f, full = -1.f;
	ComputeActorReach(r, 1.f, 1.f, footprint, full);
	EXPECT_EQ(0.f, footprint);
	EXPECT_EQ(0.f, full);
}

TEST(ModelReachWide, OnlyPastTheRadius)
{
	EXPECT_TRUE(ComputeReachIsWide(16.5f, 16.f));
	EXPECT_FALSE(ComputeReachIsWide(16.f, 16.f));
	EXPECT_FALSE(ComputeReachIsWide(0.f, 16.f));
}

TEST(ModelReachBox, HoldsBothSpotsAndTheReach)
{
	const Box b = ComputeFootprintBox(100, 50, 80, 70, 10);
	EXPECT_DOUBLE_EQ(70, b.left);
	EXPECT_DOUBLE_EQ(40, b.bottom);
	EXPECT_DOUBLE_EQ(110, b.right);
	EXPECT_DOUBLE_EQ(80, b.top);
}

TEST(ModelReachLine, LineThroughTheBoxCrosses)
{
	const Box b = { 0, 0, 100, 100 };
	EXPECT_TRUE(ComputeBoxCrossesLine(b, -50, 50, 150, 50));
	EXPECT_TRUE(ComputeBoxCrossesLine(b, 20, 20, 30, 30));
}

TEST(ModelReachLine, LineWhoseBoundsMissDoesNot)
{
	const Box b = { 0, 0, 100, 100 };
	EXPECT_FALSE(ComputeBoxCrossesLine(b, 150, 0, 200, 100));
	EXPECT_FALSE(ComputeBoxCrossesLine(b, -100, 0, -50, 100));
	EXPECT_FALSE(ComputeBoxCrossesLine(b, 0, 150, 100, 200));
	EXPECT_FALSE(ComputeBoxCrossesLine(b, 0, -100, 100, -50));
}

// [Doruk-Aksoy] As P_CreateSecNodeList, a line along the box's edge does not take the box into its sectors.
TEST(ModelReachLine, LineAlongTheEdgeDoesNot)
{
	const Box b = { 0, 0, 100, 100 };
	EXPECT_FALSE(ComputeBoxCrossesLine(b, 100, -50, 100, 150));
}

TEST(ModelReachLine, DiagonalPassingTheCornerDoesNot)
{
	const Box b = { 0, 0, 100, 100 };
	EXPECT_FALSE(ComputeBoxCrossesLine(b, 90, 160, 160, 90));
}

TEST(ModelReachLine, DiagonalThroughTheCornerCrosses)
{
	const Box b = { 0, 0, 100, 100 };
	EXPECT_TRUE(ComputeBoxCrossesLine(b, 50, 150, 150, 50));
}

namespace {

// [Doruk-Aksoy] A 2 by 2 blockmap of 128-unit cells from (-128, -128), cells row by row from the south-west: line 4 in all but the north-east one, which is empty, and line 7 in the south-west one alone.
const int kBlockmap[] = { 0, 4, 7, 10 };
const int kLump[] = { 0, 4, 7, -1, 0, 4, -1, 0, 4, -1, 0, -1 };

} // namespace

TEST(ModelReachBlockmap, CellsUnderTheBoxEachLineOnce)
{
	std::vector<int> out;
	const Box all = { -100, -100, 100, 100 };
	ComputeBlockmapLines(kBlockmap, kLump, 2, 2, -128, -128, 128, all, out);
	EXPECT_EQ((std::vector<int>{ 4, 7 }), out);
	const Box ne = { 10, 10, 20, 20 };
	ComputeBlockmapLines(kBlockmap, kLump, 2, 2, -128, -128, 128, ne, out);
	EXPECT_TRUE(out.empty());
	const Box se = { 10, -20, 20, -10 };
	ComputeBlockmapLines(kBlockmap, kLump, 2, 2, -128, -128, 128, se, out);
	EXPECT_EQ((std::vector<int>{ 4 }), out);
}

TEST(ModelReachBlockmap, BoxPastTheMapIsClampedOrEmpty)
{
	std::vector<int> out = { 99 };
	const Box past = { 500, 500, 600, 600 };
	ComputeBlockmapLines(kBlockmap, kLump, 2, 2, -128, -128, 128, past, out);
	EXPECT_TRUE(out.empty());
	const Box over = { -1000, -1000, -120, -120 };
	ComputeBlockmapLines(kBlockmap, kLump, 2, 2, -128, -128, 128, over, out);
	EXPECT_EQ((std::vector<int>{ 4, 7 }), out);
}

TEST(ModelReachFootprint, CentreAndBothSidesOfEveryCrossingLine)
{
	const FootLine lines[] = {
		{ -50, 50, 150, 50, 3, 1 },     // [Doruk-Aksoy] through the box
		{ 200, 0, 200, 100, 8, 9 },     // [Doruk-Aksoy] past it
		{ 50, -50, 50, 150, 5, -1 },    // [Doruk-Aksoy] one-sided, through it
		{ 10, -50, 10, 150, -1, 6 },    // [Doruk-Aksoy] a broken map's line with no front, through it
	};
	const Box b = { 0, 0, 100, 100 };
	std::vector<int> out = { 42 };
	ComputeFootprintSectors(lines, 4, b, 3, out);
	EXPECT_EQ((std::vector<int>{ 1, 3, 5, 6 }), out);
	ComputeFootprintSectors(lines, 0, b, -1, out);
	EXPECT_TRUE(out.empty());
}

TEST(ModelReachReached, AnyStampedSectorWithinTheStamps)
{
	const unsigned stamps[] = { 7, 3, 7 };
	const int some[] = { 1, 2 };
	const int none[] = { 1, -1, 5 };
	EXPECT_TRUE(ComputeAnyReached(some, 2, stamps, 3, 7));
	EXPECT_FALSE(ComputeAnyReached(none, 3, stamps, 3, 7));
	EXPECT_FALSE(ComputeAnyReached(some, 0, stamps, 3, 7));
}
