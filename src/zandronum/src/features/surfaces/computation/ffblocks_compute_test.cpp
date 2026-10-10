// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/surfaces/computation/ffblocks_compute.h"

using namespace zx::surfaces;

namespace {

FFRover Slab(float bottom, float top, bool sides = true, bool inverted = false, int kind = 0)
{
	FFRover r;
	r.top[0] = r.top[1] = top;
	r.bottom[0] = r.bottom[1] = bottom;
	r.renderSides = sides;
	r.invertSides = inverted;
	r.kind = kind;
	return r;
}

// [rc4l] A swimmable translucent slab, and a translucent one that is not swimmable: two kinds that never clip each other.
const int kWater = 3, kGlass = 2;
FFRover Water(float bottom, float top) { return Slab(bottom, top, true, false, kWater); }

} // namespace

TEST(FFBlocks, OneSlabCutsOneBlock)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[1] = { Slab(64.f, 96.f) };
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFBlocks(top, bottom, slabs, 1, out, 8));
	EXPECT_FLOAT_EQ(96.f, out[0].top[0]);
	EXPECT_FLOAT_EQ(64.f, out[0].bottom[0]);
	EXPECT_EQ(0, out[0].rover);
}

TEST(FFBlocks, SlabsAreWalkedTopDown)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[2] = { Slab(160.f, 192.f), Slab(64.f, 96.f) };
	FFBlock out[8];
	ASSERT_EQ(2, ComputeFFBlocks(top, bottom, slabs, 2, out, 8));
	EXPECT_FLOAT_EQ(192.f, out[0].top[0]);
	EXPECT_FLOAT_EQ(96.f, out[1].top[0]);
}

// [rc4l] A slab reaching above what the previous one left is clipped to it, not drawn over it.
TEST(FFBlocks, AnOverlappingSlabIsClippedToTheRunningTop)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[2] = { Slab(128.f, 192.f), Slab(64.f, 160.f) };
	FFBlock out[8];
	ASSERT_EQ(2, ComputeFFBlocks(top, bottom, slabs, 2, out, 8));
	EXPECT_FLOAT_EQ(128.f, out[1].top[0]);   // clipped down from 160 to where the first slab ended
	EXPECT_FLOAT_EQ(64.f, out[1].bottom[0]);
}

// One entirely above the wall contributes nothing -- while nothing has been drawn yet, which is the
// condition GL writes and the reason the renderedSomething flag exists at all.
TEST(FFBlocks, ASlabAboveTheWallIsSkipped)
{
	const float top[2] = { 128.f, 128.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[2] = { Slab(300.f, 400.f), Slab(32.f, 64.f) };
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFBlocks(top, bottom, slabs, 2, out, 8));
	EXPECT_FLOAT_EQ(64.f, out[0].top[0]);
	EXPECT_EQ(1, out[0].rover);
}

TEST(FFBlocks, SlabsThatDrawNoSidesAreSkipped)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[2] = { Slab(160.f, 192.f, false), Slab(64.f, 96.f) };
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFBlocks(top, bottom, slabs, 2, out, 8));
	EXPECT_EQ(1, out[0].rover);
}

// An inverted slab is the FRONT sector's business -- InverseFloors draws it, from the other side.
TEST(FFBlocks, InvertedSlabsBelongToTheOtherPass)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[1] = { Slab(64.f, 96.f, true, true) };
	FFBlock out[8];
	EXPECT_EQ(0, ComputeFFBlocks(top, bottom, slabs, 1, out, 8));
}

// The walk stops once the running top has reached the wall's bottom: everything below is the wall's
// own lower part, already drawn.
TEST(FFBlocks, TheWalkStopsAtTheWallsBottom)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 128.f, 128.f };
	const FFRover slabs[3] = { Slab(160.f, 192.f), Slab(64.f, 128.f), Slab(0.f, 32.f) };
	FFBlock out[8];
	const int n = ComputeFFBlocks(top, bottom, slabs, 3, out, 8);
	EXPECT_LE(n, 2);
	for (int i = 0; i < n; i++) EXPECT_LT(out[i].rover, 2);
}

// A sloped slab keeps each end at its own height, which is why the pair is passed rather than one
// number -- the same reason the light bands take a pair.
TEST(FFBlocks, ASlopedSlabKeepsBothEnds)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	FFRover s = Slab(64.f, 96.f);
	s.top[1] = 160.f; s.bottom[1] = 120.f;
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFBlocks(top, bottom, &s, 1, out, 8));
	EXPECT_FLOAT_EQ(96.f, out[0].top[0]);
	EXPECT_FLOAT_EQ(160.f, out[0].top[1]);
	EXPECT_FLOAT_EQ(120.f, out[0].bottom[1]);
}

TEST(FFBlocks, TheOutputIsBounded)
{
	const float top[2] = { 1024.f, 1024.f }, bottom[2] = { 0.f, 0.f };
	FFRover slabs[10];
	for (int i = 0; i < 10; i++) slabs[i] = Slab(900.f - i * 100.f, 950.f - i * 100.f);
	FFBlock out[3];
	EXPECT_LE(ComputeFFBlocks(top, bottom, slabs, 10, out, 3), 3);
}

// [rc4l] wdiexsec's dock: its water (-154..-80) meets the pool's (-194..-80), which covers it, so GL draws no wall at the seam and we drew a blue box.
TEST(FFBlocks, WaterMeetingDeeperWaterOfItsKindDrawsNoWall)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { -154.f, -154.f };
	const FFRover dock[1] = { Water(-154.f, -80.f) };
	const FFRover pool[1] = { Water(-194.f, -80.f) };
	FFBlock out[8];
	EXPECT_EQ(0, ComputeFFBlocks(top, bottom, dock, 1, out, 8, pool, 1));
	EXPECT_EQ(1, ComputeFFBlocks(top, bottom, dock, 1, out, 8));
}

// [rc4l] From the dock's side the pool's water is left only below the dock's own, under the floor.
TEST(FFBlocks, ShallowerWaterOnTheNearSideLeavesThePartBelowIt)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { -154.f, -154.f };
	const FFRover pool[1] = { Water(-194.f, -80.f) };
	const FFRover dock[1] = { Water(-170.f, -80.f) };
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFBlocks(top, bottom, pool, 1, out, 8, dock, 1));
	EXPECT_FLOAT_EQ(-170.f, out[0].top[0]);
	EXPECT_FLOAT_EQ(-194.f, out[0].bottom[1]);
	EXPECT_EQ(0, out[0].rover);
}

TEST(FFBlocks, ASolidSlabIsNeverClipped)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { -154.f, -154.f };
	const FFRover deck[1] = { Slab(-64.f, -42.f) };
	const FFRover near[1] = { Slab(-64.f, -42.f) };
	FFBlock out[8];
	EXPECT_EQ(1, ComputeFFBlocks(top, bottom, deck, 1, out, 8, near, 1));
}

TEST(FFBlocks, OnlyTheSameKindClips)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { -154.f, -154.f };
	const FFRover water[1] = { Water(-154.f, -80.f) };
	const FFRover near[2] = { Slab(-200.f, -60.f, true, false, kGlass), Slab(-200.f, -60.f, false, false, kWater) };
	FFBlock out[8];
	EXPECT_EQ(1, ComputeFFBlocks(top, bottom, water, 1, out, 8, near, 2));
}

TEST(FFBlocks, ANearSlabInsideTheBlockSplitsIt)
{
	const float top[2] = { -80.f, -80.f }, bottom[2] = { -154.f, -154.f };
	const FFRover near[1] = { Water(-120.f, -100.f) };
	FFBlock out[8];
	ASSERT_EQ(2, ComputeFFClip(top, bottom, kWater, near, 1, 4, out, 8));
	EXPECT_FLOAT_EQ(-80.f, out[0].top[0]);
	EXPECT_FLOAT_EQ(-100.f, out[0].bottom[0]);
	EXPECT_FLOAT_EQ(-120.f, out[1].top[0]);
	EXPECT_FLOAT_EQ(-154.f, out[1].bottom[0]);
	EXPECT_EQ(4, out[1].rover);
}

TEST(FFBlocks, ANearSlabInsideReachingTheBottomLeavesOnlyThePartAbove)
{
	const float top[2] = { -80.f, -80.f }, bottom[2] = { -154.f, -154.f };
	const FFRover near[1] = { Water(-160.f, -100.f) };
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFClip(top, bottom, kWater, near, 1, 0, out, 8));
	EXPECT_FLOAT_EQ(-100.f, out[0].bottom[0]);
}

// [rc4l] Slabs are held top down, so the first one wholly below the block ends the clipping, as GL's goto done does.
TEST(FFBlocks, NearSlabsAboveAreSkippedAndOneBelowEndsTheClip)
{
	const float top[2] = { -80.f, -80.f }, bottom[2] = { -154.f, -154.f };
	const FFRover near[3] = { Water(-40.f, 0.f), Water(-300.f, -200.f), Water(-400.f, -60.f) };
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFClip(top, bottom, kWater, near, 3, 0, out, 8));
	EXPECT_FLOAT_EQ(-80.f, out[0].top[0]);
	EXPECT_FLOAT_EQ(-154.f, out[0].bottom[0]);
}

// [rc4l] GL leaves a slab crossing the block's edge (one end in, one out) alone rather than split it, and so does this.
TEST(FFBlocks, ACrossingNearSlabIsLeftAlone)
{
	const float top[2] = { -80.f, -80.f }, bottom[2] = { -154.f, -154.f };
	FFRover crossTop = Water(-120.f, -60.f);
	crossTop.top[1] = -90.f;
	FFRover crossBottom = Water(-120.f, -60.f);
	crossBottom.bottom[1] = -170.f;
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFClip(top, bottom, kWater, &crossTop, 1, 0, out, 8));
	EXPECT_FLOAT_EQ(-80.f, out[0].top[0]);
	ASSERT_EQ(1, ComputeFFClip(top, bottom, kWater, &crossBottom, 1, 0, out, 8));
	EXPECT_FLOAT_EQ(-80.f, out[0].top[0]);
}

TEST(FFBlocks, TheClipIsBoundedAndDropsEmptyBlocks)
{
	const float top[2] = { -80.f, -80.f }, bottom[2] = { -154.f, -154.f };
	const FFRover near[1] = { Water(-120.f, -100.f) };
	FFBlock out[1];
	EXPECT_EQ(1, ComputeFFClip(top, bottom, kWater, near, 1, 0, out, 1));
	const float upsideDown[2] = { -200.f, -200.f };
	EXPECT_EQ(0, ComputeFFClip(upsideDown, bottom, kWater, near, 0, 0, out, 1));
}

namespace {

FFRover Sloped(float topL, float topR, float botL, float botR)
{
	FFRover r = Water(botL, topL);
	r.top[1] = topR;
	r.bottom[1] = botR;
	return r;
}

} // namespace

// [rc4l] Every test GL makes needs both ends, so a slab that passes at one end and fails at the other is judged as failing.
TEST(FFBlocks, SlopedNearSlabsAreJudgedAtBothEnds)
{
	const float top[2] = { -80.f, -80.f }, bottom[2] = { -154.f, -154.f };
	FFBlock out[8];

	const FFRover belowAtOneEnd = Sloped(-160.f, -100.f, -200.f, -200.f);
	ASSERT_EQ(1, ComputeFFClip(top, bottom, kWater, &belowAtOneEnd, 1, 0, out, 8));
	EXPECT_FLOAT_EQ(-160.f, out[0].bottom[0]);
	EXPECT_FLOAT_EQ(-100.f, out[0].bottom[1]);

	const FFRover aboveAtOneEnd = Sloped(0.f, 0.f, -60.f, -100.f);
	ASSERT_EQ(1, ComputeFFClip(top, bottom, kWater, &aboveAtOneEnd, 1, 0, out, 8));
	EXPECT_FLOAT_EQ(-100.f, out[0].top[1]);

	const FFRover reachesTheBottomAtOneEnd = Sloped(0.f, 0.f, -170.f, -120.f);
	ASSERT_EQ(1, ComputeFFClip(top, bottom, kWater, &reachesTheBottomAtOneEnd, 1, 0, out, 8));
	EXPECT_FLOAT_EQ(-80.f, out[0].top[0]);

	const FFRover insideAtOneEnd = Sloped(-90.f, -60.f, -120.f, -120.f);
	ASSERT_EQ(1, ComputeFFClip(top, bottom, kWater, &insideAtOneEnd, 1, 0, out, 8));
	EXPECT_FLOAT_EQ(-80.f, out[0].top[0]);

	const FFRover insideToTheBottomAtOneEnd = Sloped(-100.f, -100.f, -160.f, -120.f);
	ASSERT_EQ(2, ComputeFFClip(top, bottom, kWater, &insideToTheBottomAtOneEnd, 1, 0, out, 8));
	EXPECT_FLOAT_EQ(-160.f, out[1].top[0]);
	EXPECT_FLOAT_EQ(-120.f, out[1].top[1]);
}
