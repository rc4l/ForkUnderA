// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/levelmesh/computation/fakeplane_compute.h"

using namespace zx::levelmesh;

namespace {

// [rc4l] A level's subsectors as lists of segs, flattened the way the glue hands them over.
struct Graph
{
	std::vector<FillSeg> segs;
	std::vector<int> first, count;
	void Sub(std::vector<FillSeg> s)
	{
		first.push_back((int)segs.size());
		count.push_back((int)s.size());
		segs.insert(segs.end(), s.begin(), s.end());
	}
	// [rc4l] One fill with marks of its own, the first stamp.
	std::vector<int> Fill(bool upper, int start, long long planez) const
	{
		std::vector<int> seen(first.size(), 0);
		return ComputeFakePlaneFill(upper, start, planez, segs.data(), first.data(), count.data(), (int)first.size(), seen, 1);
	}
};

// [rc4l] A seg into another subsector of the same sector, which the fill crosses without a check.
FillSeg Inner(int to) { FillSeg s = {}; s.backSub = to; return s; }

// [rc4l] A seg onto a neighbour sector with these far planes and no textures.
FillSeg Edge(int to, long long ceil, long long floor)
{
	FillSeg s = {};
	s.backSub = to;
	s.sectorEdge = true;
	s.farCeil = ceil;
	s.farFloor = floor;
	return s;
}

MissingTierInput Tier(long long ff, long long fc, long long bf, long long bc)
{
	MissingTierInput in = {};
	in.ffh[0] = in.ffh[1] = ff; in.fch[0] = in.fch[1] = fc;
	in.bfh[0] = in.bfh[1] = bf; in.bch[0] = in.bch[1] = bc;
	in.partnerOk = true;
	return in;
}

}

// [rc4l] WDI18's ceiling: a room at 344 inside a sector at 216, the uppers between them left untextured, so GL draws the 216 ceiling across the room.
TEST(FakePlaneCompute, AnUntexturedUpperRingFillsTheRoomWithTheLowerCeiling)
{
	Graph g;
	g.Sub({ Inner(1), Edge(2, 216, 0), Edge(3, 216, 0) });   // [rc4l] 0 and 1 are the room, 2 and 3 the sector round it
	g.Sub({ Inner(0), Edge(2, 216, 0) });
	g.Sub({});
	g.Sub({});
	EXPECT_EQ(std::vector<int>({ 1, 0 }), g.Fill(true, 0, 216));
	EXPECT_TRUE(g.Fill(true, 0, 300).empty());   // [rc4l] a neighbour ceiling below the fill height
	EXPECT_EQ(std::vector<int>({ 2, 1, 3, 0 }), g.Fill(true, 0, 100));   // [rc4l] higher neighbours are flooded into, deepest first
}

TEST(FakePlaneCompute, ATexturedEdgeASlopeOrAWallStopsTheFill)
{
	Graph textured;
	FillSeg t = Edge(1, 216, 0);
	t.topTextured = true;
	textured.Sub({ t });
	textured.Sub({});
	EXPECT_TRUE(textured.Fill(true, 0, 216).empty());
	t.topTextured = false;
	t.bottomTextured = true;
	Graph bottom;
	bottom.Sub({ t });
	bottom.Sub({});
	EXPECT_EQ(std::vector<int>({ 0 }), bottom.Fill(true, 0, 216));   // [rc4l] an upper fill reads only the upper texture
	Graph sloped;
	FillSeg s = Edge(1, 216, 0);
	s.farCeilSloped = true;
	sloped.Sub({ s });
	sloped.Sub({});
	EXPECT_TRUE(sloped.Fill(true, 0, 216).empty());
	Graph walled;
	FillSeg w = Inner(1);
	w.closed = true;
	walled.Sub({ Edge(1, 216, 0), w });
	walled.Sub({});
	EXPECT_TRUE(walled.Fill(true, 0, 216).empty());
	Graph walledBeyond;   // [rc4l] a wall one subsector in fails the whole fill
	walledBeyond.Sub({ Inner(1) });
	walledBeyond.Sub({ w });
	EXPECT_TRUE(walledBeyond.Fill(true, 0, 216).empty());
}

// [rc4l] A neighbour reached already is not checked again, as GL's validcount skips it.
TEST(FakePlaneCompute, ANeighbourIsVisitedOnce)
{
	Graph g;
	g.Sub({ Inner(1), Edge(1, 100, 0) });   // [rc4l] the second edge would fail, but 1 is already reached
	g.Sub({ Edge(0, 216, 0) });
	EXPECT_EQ(std::vector<int>({ 1, 0 }), g.Fill(true, 0, 216));
}

// [rc4l] WDI18's rug turned the other way: a recess whose untextured steps GL fills with the room's floor.
TEST(FakePlaneCompute, ALowerFillMirrorsTheUpperOne)
{
	Graph g;
	g.Sub({ Edge(1, 128, 0) });
	g.Sub({});
	EXPECT_EQ(std::vector<int>({ 0 }), g.Fill(false, 0, 0));
	EXPECT_TRUE(g.Fill(false, 0, -16).empty());   // [rc4l] a neighbour floor above the fill height
	FillSeg up = Edge(1, 128, -32);
	Graph deeper;
	deeper.Sub({ up });
	deeper.Sub({});
	EXPECT_EQ(std::vector<int>({ 1, 0 }), deeper.Fill(false, 0, 0));
	up.farFloorSloped = true;
	Graph sloped;
	sloped.Sub({ up });
	sloped.Sub({});
	EXPECT_TRUE(sloped.Fill(false, 0, 0).empty());
	FillSeg tex = Edge(1, 128, 0);
	tex.bottomTextured = true;
	Graph textured;
	textured.Sub({ tex });
	textured.Sub({});
	EXPECT_TRUE(textured.Fill(false, 0, 0).empty());
}

TEST(FakePlaneCompute, ABadIndexFillsNothing)
{
	Graph g;
	g.Sub({ Inner(5) });
	EXPECT_TRUE(g.Fill(true, 0, 0).empty());
	EXPECT_TRUE(g.Fill(true, -1, 0).empty());
	EXPECT_TRUE(g.Fill(true, 1, 0).empty());
	Graph neg;
	neg.Sub({ Inner(-1) });
	EXPECT_TRUE(neg.Fill(true, 0, 0).empty());
}

TEST(FakePlaneCompute, AnUpperIsFiledWhenTheBackCeilingCutsBelowAndNothingTexturesIt)
{
	long long z = 0;
	MissingTierInput in = Tier(0, 344, 0, 216);
	ASSERT_TRUE(ComputeMissingUpper(in, z));
	EXPECT_EQ(216, z);
	in.bch[0] = 344;   // [rc4l] one end cutting below is enough
	EXPECT_TRUE(ComputeMissingUpper(in, z));
	in = Tier(0, 216, 0, 216);
	EXPECT_FALSE(ComputeMissingUpper(in, z));
	in = Tier(0, 344, 0, 216);
	in.textured = true;
	EXPECT_FALSE(ComputeMissingUpper(in, z));
	in = Tier(0, 344, 0, 216);
	in.frontCeilSky = in.backCeilSky = true;
	EXPECT_FALSE(ComputeMissingUpper(in, z));
	in = Tier(0, 344, 0, 216);
	in.sloped = true;
	EXPECT_FALSE(ComputeMissingUpper(in, z));   // [rc4l] GL paints a sloped pair with the ceiling's texture
	in.backCeilSky = true;
	EXPECT_TRUE(ComputeMissingUpper(in, z));
	in = Tier(0, 344, 0, 216);
	in.frontCeilSky = true;
	in.sloped = true;
	EXPECT_TRUE(ComputeMissingUpper(in, z));
	in = Tier(0, 344, 0, 216);
	in.polyobj = true;
	EXPECT_FALSE(ComputeMissingUpper(in, z));
	in = Tier(0, 344, 0, 216);
	in.partnerOk = false;
	EXPECT_FALSE(ComputeMissingUpper(in, z));
}

// [rc4l] The back ceiling below the front floor: the gap starts at the front floor, unless both floors are sky.
TEST(FakePlaneCompute, AnUpperUnderTheFrontFloorStartsAtThatFloor)
{
	long long z = 0;
	MissingTierInput in = Tier(100, 344, 0, 50);
	ASSERT_TRUE(ComputeMissingUpper(in, z));
	EXPECT_EQ(100, z);
	in.frontFloorSky = in.backFloorSky = true;
	ASSERT_TRUE(ComputeMissingUpper(in, z));
	EXPECT_EQ(50, z);
	in = Tier(100, 344, 0, 50);
	in.bch[1] = 200;   // [rc4l] only when the floor is above it at both ends
	ASSERT_TRUE(ComputeMissingUpper(in, z));
	EXPECT_EQ(50, z);
	in = Tier(100, 344, 0, 200);
	in.bch[0] = 50;
	ASSERT_TRUE(ComputeMissingUpper(in, z));
	EXPECT_EQ(50, z);
}

TEST(FakePlaneCompute, ALowerIsFiledWhenTheBackFloorRisesAndNothingTexturesIt)
{
	long long z = 0;
	MissingTierInput in = Tier(-16, 256, 0, 256);
	ASSERT_TRUE(ComputeMissingLower(in, z));
	EXPECT_EQ(0, z);
	in.bfh[0] = -16;
	EXPECT_TRUE(ComputeMissingLower(in, z));
	in = Tier(0, 256, 0, 256);
	EXPECT_FALSE(ComputeMissingLower(in, z));
	in = Tier(-16, 256, 0, 256);
	in.textured = true;
	EXPECT_FALSE(ComputeMissingLower(in, z));
	in = Tier(-16, 256, 0, 256);
	in.sloped = true;
	EXPECT_FALSE(ComputeMissingLower(in, z));
	in.frontFloorSky = true;
	EXPECT_TRUE(ComputeMissingLower(in, z));   // [rc4l] a sloped pair with a sky floor is still filled
	in = Tier(-16, 256, 0, 256);
	in.backFloorSky = true;
	EXPECT_FALSE(ComputeMissingLower(in, z));
	in = Tier(-16, 256, 0, 256);
	in.polyobj = true;
	EXPECT_FALSE(ComputeMissingLower(in, z));
	in = Tier(-16, 256, 0, 256);
	in.partnerOk = false;
	EXPECT_FALSE(ComputeMissingLower(in, z));
	in = Tier(-16, 40, 64, 256);   // [rc4l] the front ceiling comes down past the back floor
	ASSERT_TRUE(ComputeMissingLower(in, z));
	EXPECT_EQ(40, z);
	in.fch[1] = 100;
	ASSERT_TRUE(ComputeMissingLower(in, z));
	EXPECT_EQ(64, z);
}

TEST(FakePlaneCompute, ASubsectorKeepsTheLowestCeilingOrHighestFloorItBorders)
{
	std::vector<MissingEntry> up;
	std::vector<int> upOf(10, -1), downOf(10, -1);
	ComputeFileMissing(up, upOf, true, 7, 216, 3421);
	ComputeFileMissing(up, upOf, true, 7, 300, 9);
	ComputeFileMissing(up, upOf, true, 7, 128, 12);
	ComputeFileMissing(up, upOf, true, 8, 216, 3421);
	ASSERT_EQ(2u, up.size());
	EXPECT_EQ(128, up[0].planez);
	EXPECT_EQ(12, up[0].model);
	EXPECT_EQ(8, up[1].sub);
	std::vector<MissingEntry> down;
	ComputeFileMissing(down, downOf, false, 7, 0, 1);
	ComputeFileMissing(down, downOf, false, 7, -8, 2);
	ComputeFileMissing(down, downOf, false, 7, 16, 3);
	ASSERT_EQ(1u, down.size());
	EXPECT_EQ(16, down[0].planez);
	EXPECT_EQ(3, down[0].model);
}

// [rc4l] One fill over several subsectors settles their entries; a failed one draws nothing and settles nothing.
TEST(FakePlaneCompute, EachSucceedingFillPutsItsModelsPlaneOverWhatItCovers)
{
	Graph g;
	g.Sub({ Inner(1), Edge(2, 216, 0) });
	g.Sub({ Inner(0), Edge(2, 216, 0) });
	g.Sub({});
	g.Sub({ Edge(2, 100, 0) });   // [rc4l] a lone subsector whose neighbour ceiling is too low
	g.Sub({ Edge(2, 216, 0) });   // [rc4l] a lone subsector that fills only itself
	const std::vector<MissingEntry> entries = { { 0, 216, 3421 }, { 1, 216, 3421 }, { 3, 216, 9 }, { 4, 216, 5 } };
	const std::vector<FakePlane> planes = ComputeFakePlanes(true, entries, g.segs.data(), g.first.data(), g.count.data(), (int)g.first.size());
	ASSERT_EQ(3u, planes.size());
	EXPECT_EQ(1, planes[0].sub);
	EXPECT_EQ(0, planes[1].sub);
	EXPECT_EQ(3421, planes[1].model);
	EXPECT_TRUE(planes[1].upper);
	EXPECT_EQ(4, planes[2].sub);
	EXPECT_EQ(5, planes[2].model);
}

TEST(FakePlaneCompute, PlanesGroupBySubsectorOrModelKeepingTheirOrder)
{
	const std::vector<FakePlane> planes = { { 2, 9, true }, { 0, 5, true }, { 2, 4, false }, { 1, 9, true } };
	std::vector<FakePlane> out;
	std::vector<int> start;
	ComputeGroupPlanes(planes, 3, false, out, start);
	EXPECT_EQ(std::vector<int>({ 0, 1, 2, 4 }), start);
	EXPECT_EQ(5, out[0].model);
	EXPECT_EQ(9, out[2].model);   // [rc4l] subsector 2's two planes in the order given
	EXPECT_EQ(4, out[3].model);
	ComputeGroupPlanes(planes, 10, true, out, start);
	EXPECT_EQ(2, start[9]);   // [rc4l] sectors 4 and 5 come first
	EXPECT_EQ(4, start[10]);
	EXPECT_EQ(2, out[2].sub);   // [rc4l] sector 9's subsectors, 2 then 1
	EXPECT_EQ(1, out[3].sub);
}

// [rc4l] Only a subsector whose planes came, went or changed is baked again, and a first pass counts every filled one.
TEST(FakePlaneCompute, OnlySubsectorsWhosePlanesDifferAreReported)
{
	std::vector<FakePlane> before, now;
	std::vector<int> beforeStart, nowStart;
	ComputeGroupPlanes({ { 0, 5, true }, { 2, 9, true } }, 4, false, before, beforeStart);
	ComputeGroupPlanes({ { 0, 5, true }, { 1, 7, false }, { 2, 9, false } }, 4, false, now, nowStart);
	EXPECT_EQ(std::vector<int>({ 1, 2 }), ComputeChangedSubs(before, beforeStart, now, nowStart, 4));
	EXPECT_TRUE(ComputeChangedSubs(now, nowStart, now, nowStart, 4).empty());
	std::vector<FakePlane> moved;
	std::vector<int> movedStart;
	ComputeGroupPlanes({ { 0, 6, true }, { 1, 7, false }, { 2, 9, false } }, 4, false, moved, movedStart);
	EXPECT_EQ(std::vector<int>({ 0 }), ComputeChangedSubs(now, nowStart, moved, movedStart, 4));
	EXPECT_EQ(std::vector<int>({ 0, 1, 2 }), ComputeChangedSubs({}, {}, now, nowStart, 4));
}

TEST(FakePlaneCompute, AnIndexByKeyListsEachKeysItemsInOrder)
{
	std::vector<int> start, order;
	ComputeIndexByKey({ 2, 0, 2, 1 }, 4, start, order);
	EXPECT_EQ(std::vector<int>({ 0, 1, 2, 4, 4 }), start);
	EXPECT_EQ(std::vector<int>({ 1, 3, 0, 2 }), order);
}

// [rc4l] WDI18's mirror frame columns: floor 128 over ceiling 88 reads to GL as a sector shut at 128.
TEST(FakePlaneCompute, ABackSectorWhoseFloorIsAboveItsCeilingIsClosedAtItsFloor)
{
	EXPECT_TRUE(ComputeClosedAtFloor(false, 128, 88));
	EXPECT_FALSE(ComputeClosedAtFloor(false, 0, 128));
	EXPECT_FALSE(ComputeClosedAtFloor(false, 88, 88));
	EXPECT_FALSE(ComputeClosedAtFloor(true, 128, 88));
}

// [rc4l] Every clause of the gate stops a seg on its own.
TEST(FakePlaneCompute, OnlyAPlainTwoSidedSegOfItsOwnSubsectorCanLeaveAGap)
{
	EXPECT_TRUE(ComputeIsFillCandidate(true, true, false, true, true, false));
	EXPECT_FALSE(ComputeIsFillCandidate(false, true, false, true, true, false));   // [rc4l] a miniseg
	EXPECT_FALSE(ComputeIsFillCandidate(true, false, false, true, true, false));
	EXPECT_FALSE(ComputeIsFillCandidate(true, true, true, true, true, false));
	EXPECT_FALSE(ComputeIsFillCandidate(true, true, false, false, true, false));
	EXPECT_FALSE(ComputeIsFillCandidate(true, true, false, true, false, false));
	EXPECT_FALSE(ComputeIsFillCandidate(true, true, false, true, true, true));
}
