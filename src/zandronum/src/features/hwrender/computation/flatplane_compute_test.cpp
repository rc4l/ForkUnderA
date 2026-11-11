// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "flatplane_compute.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace zx::hwrender;

namespace {

FlatOrderItem Item(float dist, float lo, float hi, FlatFootprint area, bool plane = false)
{
	FlatOrderItem it;
	it.dist = dist; it.lo = lo; it.hi = hi; it.area = area; it.plane = plane;
	return it;
}

const FlatFootprint kDisc = { -96.f, 96.f, -96.f, 96.f };
const FlatFootprint kMonster = { 20.f, 60.f, 40.f, 44.f };
const FlatFootprint kElsewhere = { 500.f, 540.f, 500.f, 504.f };

// [Doruk-Aksoy] One scratch for every test, so a later call reuses what an earlier one left.
FlatOrderScratch &Scratch()
{
	static FlatOrderScratch s;
	return s;
}

std::vector<int> Found(FlatGrid &g, FlatFootprint area)
{
	std::vector<int> out;
	g.Query(area, [&](int i) { out.push_back(i); });
	std::sort(out.begin(), out.end());
	return out;
}

}

TEST(FlatPlane, OnlyAFlaggedThinBlendedModelSurfaceIsAPlane)
{
	EXPECT_TRUE(ComputeIsFlatPlane(true, true, 1, false, 1.f, 1.f));
	EXPECT_TRUE(ComputeIsFlatPlane(true, true, 2, false, 1.f, 1.f + kFlatSliver));   // [Doruk-Aksoy] a disc with a raised rim
	EXPECT_FALSE(ComputeIsFlatPlane(false, true, 1, false, 1.f, 1.f));  // [Doruk-Aksoy] not in FUAMODEL, as Complex Doom's blood pools are not
	EXPECT_FALSE(ComputeIsFlatPlane(true, true, 1, false, 1.f, 14.f));  // [Doruk-Aksoy] a model with depth
	EXPECT_FALSE(ComputeIsFlatPlane(true, false, 1, false, 1.f, 1.f));  // [Doruk-Aksoy] a sprite lying flat
	EXPECT_FALSE(ComputeIsFlatPlane(true, true, 0, false, 1.f, 1.f));   // [Doruk-Aksoy] an opaque model writes depth
	EXPECT_FALSE(ComputeIsFlatPlane(true, true, 1, true, 1.f, 1.f));    // [Doruk-Aksoy] the HUD's
	EXPECT_FALSE(ComputeIsFlatPlane(true, true, 1, false, 1e30f, -1e30f));   // [Doruk-Aksoy] no vertices
}

TEST(FlatPlane, FootprintsOverlapIncludingTouchingEdges)
{
	EXPECT_TRUE(ComputeFootprintsOverlap(kDisc, kMonster));
	EXPECT_TRUE(ComputeFootprintsOverlap(kDisc, { 96.f, 120.f, 0.f, 1.f }));
	EXPECT_FALSE(ComputeFootprintsOverlap(kDisc, kElsewhere));
	EXPECT_FALSE(ComputeFootprintsOverlap(kDisc, { 0.f, 1.f, 97.f, 120.f }));
	EXPECT_FALSE(ComputeFootprintsOverlap(kDisc, { -200.f, -97.f, 0.f, 1.f }));
	EXPECT_FALSE(ComputeFootprintsOverlap(kDisc, { 0.f, 1.f, -200.f, -97.f }));
}

TEST(FlatPlane, OnlyAReachPastTheSliverOnBothSidesIsSplit)
{
	EXPECT_TRUE(ComputeStraddlesHeight(0.f, 56.f, 24.f));     // [Doruk-Aksoy] a ring at the waist
	EXPECT_FALSE(ComputeStraddlesHeight(-10.f, 55.f, 1.f));   // [Doruk-Aksoy] a corpse sunk into the floor under a blood pool
	EXPECT_FALSE(ComputeStraddlesHeight(-55.f, 2.f, 0.f));
}

TEST(FlatPlane, TheGridFindsEachOverlapOnce)
{
	const FlatFootprint areas[] = { kDisc, kMonster, kElsewhere, { -8192.f, 8192.f, -8192.f, 8192.f }, { 100.f, 110.f, 100.f, 110.f } };
	FlatGrid g;
	g.Build(areas, 5, 128.f, [](int i) { return i != 1; });
	EXPECT_EQ(Found(g, kMonster), (std::vector<int>{ 0, 3 }));          // [Doruk-Aksoy] 1 was not filed; 3 is too wide for cells
	EXPECT_EQ(Found(g, kDisc), (std::vector<int>{ 0, 3 }));             // [Doruk-Aksoy] 4 shares a cell with the disc, not its footprint
	EXPECT_EQ(Found(g, { -9000.f, 9000.f, -9000.f, 9000.f }), (std::vector<int>{ 0, 2, 3, 4 }));
	EXPECT_EQ(Found(g, { -300.f, -250.f, -300.f, -250.f }), (std::vector<int>{ 3 }));
}

TEST(FlatPlane, AnUprightQuadSplitsIntoTwoQuadsAtTheHeight)
{
	const FlatSplitVertex q[4] = { { 0, 0, 56, 0, 0 }, { 40, 0, 56, 1, 0 }, { 0, 0, -1, 0, 1 }, { 40, 0, -1, 1, 1 } };
	FlatSplitVertex a[4], b[4];
	ASSERT_TRUE(ComputeSplitQuadAtHeight(q, 18.f, a, b));
	EXPECT_EQ(a[0].h, 56.f); EXPECT_EQ(a[1].h, 56.f); EXPECT_EQ(a[2].h, 18.f); EXPECT_EQ(a[3].h, 18.f);
	EXPECT_EQ(b[0].h, 18.f); EXPECT_EQ(b[1].h, 18.f); EXPECT_EQ(b[2].h, -1.f); EXPECT_EQ(b[3].h, -1.f);
	EXPECT_FLOAT_EQ(a[2].v, 38.f / 57.f);
	EXPECT_FLOAT_EQ(a[3].u, 1.f);
	EXPECT_EQ(a[2].x, b[0].x); EXPECT_EQ(a[3].x, b[1].x);
}

TEST(FlatPlane, ASlantedQuadSplitsAlongEachEdgeAtItsOwnPoint)
{
	const FlatSplitVertex q[4] = { { 0, 0, 60, 0, 0 }, { 40, 10, 40, 1, 0 }, { 10, 0, 0, 0, 1 }, { 50, 10, -20, 1, 1 } };
	FlatSplitVertex a[4], b[4];
	ASSERT_TRUE(ComputeSplitQuadAtHeight(q, 20.f, a, b));
	EXPECT_FLOAT_EQ(a[2].x, 10.f * 40.f / 60.f);
	EXPECT_FLOAT_EQ(a[3].x, 40.f + 10.f / 3.f);
	EXPECT_FLOAT_EQ(a[3].y, 10.f);
	EXPECT_EQ(a[2].h, 20.f);
}

TEST(FlatPlane, AQuadUpsideDownSplitsTheSameWay)
{
	const FlatSplitVertex q[4] = { { 0, 0, -1, 0, 1 }, { 40, 0, -1, 1, 1 }, { 0, 0, 56, 0, 0 }, { 40, 0, 56, 1, 0 } };
	FlatSplitVertex a[4], b[4];
	ASSERT_TRUE(ComputeSplitQuadAtHeight(q, 18.f, a, b));
	EXPECT_EQ(a[0].h, -1.f); EXPECT_EQ(a[2].h, 18.f); EXPECT_EQ(b[3].h, 56.f);
}

TEST(FlatPlane, AQuadAnEdgeOfWhichMissesTheHeightIsLeftWhole)
{
	const FlatSplitVertex rolled[4] = { { 0, 0, 60, 0, 0 }, { 40, 0, 10, 1, 0 }, { 0, 0, 30, 0, 1 }, { 40, 0, -20, 1, 1 } };
	FlatSplitVertex a[4], b[4];
	EXPECT_FALSE(ComputeSplitQuadAtHeight(rolled, 20.f, a, b));   // [Doruk-Aksoy] edge 0-2 stays above
	EXPECT_FALSE(ComputeSplitQuadAtHeight(rolled, 0.f, a, b));    // [Doruk-Aksoy] edge 1-3 crosses, 0-2 does not
	const FlatSplitVertex q[4] = { { 0, 0, 56, 0, 0 }, { 40, 0, 56, 1, 0 }, { 0, 0, 0, 0, 1 }, { 40, 0, 0, 1, 1 } };
	EXPECT_FALSE(ComputeSplitQuadAtHeight(q, 0.f, a, b));         // [Doruk-Aksoy] touching is not crossing
}

TEST(FlatPlane, JustBehindIsAlwaysFarther)
{
	EXPECT_GT(ComputeJustBehind(0.f), 0.f);
	EXPECT_GT(ComputeJustBehind(1e8f), 1e8f);
	EXPECT_GT(ComputeJustBehind(100.f), 100.f);
}

TEST(FlatPlaneOrder, WithNoPlaneNothingMoves)
{
	std::vector<FlatOrderItem> it = { Item(100.f, 0.f, 56.f, kMonster), Item(50.f, 0.f, 1.f, kDisc) };
	ComputeFlatPlaneOrder(it.data(), (int)it.size(), 41.f, Scratch());
	EXPECT_EQ(it[0].dist, 100.f);
	EXPECT_EQ(it[1].dist, 50.f);
}

TEST(FlatPlaneOrder, SeenFromAboveTheDiscGoesBehindWhatStandsOnIt)
{
	// [Doruk-Aksoy] The monster is farther than the disc's centre, which drew the disc over its feet; its sink below the disc is within the sliver.
	std::vector<FlatOrderItem> it = { Item(400.f, 1.f, 1.f, kDisc, true), Item(900.f, -1.f, 55.f, kMonster) };
	ComputeFlatPlaneOrder(it.data(), (int)it.size(), 41.f, Scratch());
	EXPECT_GT(it[0].dist, it[1].dist);
	EXPECT_EQ(it[1].dist, 900.f);
}

TEST(FlatPlaneOrder, WhatIsWhollyBeyondGoesBehindThePlane)
{
	// [Doruk-Aksoy] A split monster's legs under a ring, and a monster in a pit under it.
	std::vector<FlatOrderItem> it = { Item(400.f, 24.f, 24.f, kDisc, true), Item(100.f, 0.f, 24.f, kMonster),
		Item(50.f, -64.f, -8.f, kMonster) };
	ComputeFlatPlaneOrder(it.data(), (int)it.size(), 41.f, Scratch());
	EXPECT_GT(it[1].dist, it[0].dist);
	EXPECT_GT(it[2].dist, it[0].dist);
}

TEST(FlatPlaneOrder, SeenFromBelowWhatStandsOnItGoesBehindIt)
{
	std::vector<FlatOrderItem> it = { Item(400.f, 97.f, 97.f, kDisc, true), Item(100.f, 97.f, 153.f, kMonster),
		Item(800.f, 0.f, 56.f, kMonster) };
	ComputeFlatPlaneOrder(it.data(), (int)it.size(), 41.f, Scratch());
	EXPECT_GT(it[1].dist, it[0].dist);   // [Doruk-Aksoy] on top of the bridge: beyond it from below
	EXPECT_GT(it[0].dist, it[2].dist);   // [Doruk-Aksoy] under it: the eye's side
}

TEST(FlatPlaneOrder, StraddlersElsewhereAndOtherPlanesKeepTheirPlace)
{
	std::vector<FlatOrderItem> it = { Item(400.f, 1.f, 1.f, kDisc, true), Item(900.f, -20.f, 56.f, kMonster),
		Item(990.f, 1.f, 56.f, kElsewhere), Item(998.f, 1.f, 56.f, { 100.f, 110.f, 100.f, 110.f }),
		Item(390.f, 1.5f, 1.5f, kDisc, true) };
	ComputeFlatPlaneOrder(it.data(), (int)it.size(), 41.f, Scratch());
	EXPECT_EQ(it[0].dist, 400.f);
	EXPECT_EQ(it[1].dist, 900.f);   // [Doruk-Aksoy] crosses the disc by more than the sliver and was not split
	EXPECT_EQ(it[2].dist, 990.f);
	EXPECT_EQ(it[3].dist, 998.f);   // [Doruk-Aksoy] in the disc's grid cell, off its footprint
	EXPECT_EQ(it[4].dist, 390.f);   // [Doruk-Aksoy] a plane never moves another
}

TEST(FlatPlaneOrder, PlanesAsFarAboveAsBelowTheEyeAreTakenInListOrder)
{
	// [Doruk-Aksoy] A monster between a floor disc and a ceiling disc goes after both, whichever is placed first.
	std::vector<FlatOrderItem> it = { Item(400.f, 81.f, 81.f, kDisc, true), Item(300.f, 1.f, 1.f, kDisc, true),
		Item(900.f, 30.f, 50.f, kMonster) };
	ComputeFlatPlaneOrder(it.data(), (int)it.size(), 41.f, Scratch());
	EXPECT_GT(it[0].dist, it[2].dist);
	EXPECT_GT(it[1].dist, it[2].dist);
	EXPECT_EQ(it[2].dist, 900.f);
}

TEST(FlatPlaneOrder, ADrawBetweenTwoPlanesIsPlacedByTheNearerOneFirst)
{
	// [Doruk-Aksoy] From above: a low disc, a high ring, and legs split between them, which go behind the ring and in front of the disc.
	std::vector<FlatOrderItem> it = { Item(400.f, 1.f, 1.f, kDisc, true), Item(380.f, 30.f, 30.f, kDisc, true),
		Item(900.f, 1.f, 30.f, kMonster), Item(850.f, 30.f, 56.f, kMonster) };
	ComputeFlatPlaneOrder(it.data(), (int)it.size(), 100.f, Scratch());
	EXPECT_GT(it[1].dist, it[3].dist);   // [Doruk-Aksoy] the ring behind the upper body over it
	EXPECT_GT(it[2].dist, it[1].dist);   // [Doruk-Aksoy] the legs behind the ring
	EXPECT_GT(it[0].dist, it[2].dist);   // [Doruk-Aksoy] the disc behind the legs on it
}

TEST(FlatPlane, AnyBetweenIsStrictOnBothEnds)
{
	const std::vector<float> h = { 0.f, 24.f, 96.f };
	EXPECT_TRUE(ComputeAnyBetween(h, 12.f, 44.f));
	EXPECT_FALSE(ComputeAnyBetween(h, 24.f, 44.f));
	EXPECT_FALSE(ComputeAnyBetween(h, 12.f, 24.f));
	EXPECT_FALSE(ComputeAnyBetween(h, 100.f, 200.f));
	EXPECT_FALSE(ComputeAnyBetween({}, -1.f, 1.f));
}

TEST(FlatPlane, ASideHoldsAPieceUnlessMoreThanASliverIsOnTheOther)
{
	EXPECT_EQ(ComputeFlatSide(-1.f, 55.f, 1.f, true), 1);      // [Doruk-Aksoy] standing on a floor disc, feet sunk 2
	EXPECT_EQ(ComputeFlatSide(-64.f, -8.f, 1.f, true), -1);    // [Doruk-Aksoy] in a pit under it
	EXPECT_EQ(ComputeFlatSide(0.f, 24.f, 24.f, true), -1);     // [Doruk-Aksoy] legs under a ring
	EXPECT_EQ(ComputeFlatSide(24.f, 56.f, 24.f, true), 1);     // [Doruk-Aksoy] the body over it
	EXPECT_EQ(ComputeFlatSide(-20.f, 56.f, 1.f, true), 0);     // [Doruk-Aksoy] across it by more than a sliver each way
	EXPECT_EQ(ComputeFlatSide(94.f, 150.f, 97.f, false), -1);  // [Doruk-Aksoy] on a bridge seen from below, sunk 3 into it
	EXPECT_EQ(ComputeFlatSide(0.f, 56.f, 97.f, false), 1);     // [Doruk-Aksoy] under the bridge
	EXPECT_EQ(ComputeFlatSide(0.8f, 1.2f, 1.f, true), 0);      // [Doruk-Aksoy] level with it
	EXPECT_EQ(ComputeFlatSide(-3.f, 3.f, 0.f, true), 1);       // [Doruk-Aksoy] as much each side, within a sliver: the eye's
}
