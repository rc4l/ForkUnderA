// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/levelmesh/computation/surfacebudget_compute.h"

using namespace zx::levelmesh;

// ---- ComputePieceVertexBudget ----------------------------------------------

TEST(SurfaceBudget, SimplestPieceIsFourCorners)
{
	// One seg, no other sector at either vertex: nothing to weld against, so just the quad.
	EXPECT_EQ(ComputePieceVertexBudget(1, 0, 0), 4);
}

TEST(SurfaceBudget, EachTouchingSectorAddsTwoSplitsPerVertex)
{
	// One sector at the left vertex offers a floor and a ceiling height.
	EXPECT_EQ(ComputePieceVertexBudget(1, 1, 0), 6);
	EXPECT_EQ(ComputePieceVertexBudget(1, 0, 1), 6);
	EXPECT_EQ(ComputePieceVertexBudget(1, 1, 1), 8);
	EXPECT_EQ(ComputePieceVertexBudget(1, 3, 2), 4 + 6 + 4);
}

TEST(SurfaceBudget, EachExtraSegSplitsUpperAndLowerEdge)
{
	EXPECT_EQ(ComputePieceVertexBudget(2, 0, 0), 4 + 2);
	EXPECT_EQ(ComputePieceVertexBudget(5, 0, 0), 4 + 8);
}

TEST(SurfaceBudget, SegAndVertexSplitsCompose)
{
	EXPECT_EQ(ComputePieceVertexBudget(3, 2, 1), 4 + 4 + 2 + 4);
}

TEST(SurfaceBudget, DegenerateCountsClampRatherThanUnderflow)
{
	// A budget below the four corners would size a range too small to hold its own quad.
	EXPECT_EQ(ComputePieceVertexBudget(0, 0, 0), 4);
	EXPECT_EQ(ComputePieceVertexBudget(-7, 0, 0), 4);
	EXPECT_EQ(ComputePieceVertexBudget(1, -1, -1), 4);
}

// ---- ComputeSidePieceCount -------------------------------------------------

TEST(SurfaceBudget, OneSidedLineHasOneMidPiece)
{
	EXPECT_EQ(ComputeSidePieceCount(false, 0), 1);
}

TEST(SurfaceBudget, TwoSidedLineHasUpperMidLower)
{
	EXPECT_EQ(ComputeSidePieceCount(true, 0), 3);
}

TEST(SurfaceBudget, EachFFloorBlockAddsAPiece)
{
	EXPECT_EQ(ComputeSidePieceCount(true, 4), 7);
	EXPECT_EQ(ComputeSidePieceCount(false, 2), 3);
}

TEST(SurfaceBudget, NegativeFFloorCountIsIgnored)
{
	EXPECT_EQ(ComputeSidePieceCount(true, -3), 3);
}

// ---- ComputeSideVertexBudget -----------------------------------------------

TEST(SurfaceBudget, SideBudgetIsPiecesTimesPerPiece)
{
	SideBudgetInput in = {};
	in.numSegs = 2;
	in.leftVertexSectors = 1;
	in.rightVertexSectors = 1;
	in.ffloorBlocks = 0;
	in.twoSided = true;

	// per piece: 4 + 2 + 2 + 2 = 10; pieces: 3
	EXPECT_EQ(ComputeSideVertexBudget(in), 30);
}

TEST(SurfaceBudget, OneSidedWallIsTheCheapestCase)
{
	SideBudgetInput plain = {};
	plain.numSegs = 1;
	plain.twoSided = false;
	EXPECT_EQ(ComputeSideVertexBudget(plain), 4);
}

TEST(SurfaceBudget, ThreeDFloorStackingIsTheExpensiveCase)
{
	// The shape the plan flags as the affordability risk: a heavily stacked two-sided side.
	SideBudgetInput heavy = {};
	heavy.numSegs = 4;
	heavy.leftVertexSectors = 6;
	heavy.rightVertexSectors = 6;
	heavy.ffloorBlocks = 8;
	heavy.twoSided = true;

	// per piece: 4 + 12 + 12 + 6 = 34; pieces: 3 + 8 = 11
	EXPECT_EQ(ComputeSideVertexBudget(heavy), 374);
}

// ---- ComputeLevelBudget ----------------------------------------------------

TEST(SurfaceBudget, LevelBudgetSumsAndTracksTheWorstSide)
{
	SideBudgetInput sides[3] = {};
	sides[0].numSegs = 1; sides[0].twoSided = false;                       // 4
	sides[1].numSegs = 1; sides[1].twoSided = true;                        // 12
	sides[2].numSegs = 1; sides[2].twoSided = true; sides[2].ffloorBlocks = 2; // 20

	const LevelBudget b = ComputeLevelBudget(sides, 3);
	EXPECT_EQ(b.totalVertices, 36);
	EXPECT_EQ(b.sides, 3);
	EXPECT_EQ(b.maxPerSide, 20);
	EXPECT_EQ(b.worstSideIndex, 2);
}

TEST(SurfaceBudget, LevelBudgetKeepsTheFirstOfEqualWorstSides)
{
	SideBudgetInput sides[2] = {};
	sides[0].numSegs = 1; sides[0].twoSided = true;
	sides[1].numSegs = 1; sides[1].twoSided = true;

	const LevelBudget b = ComputeLevelBudget(sides, 2);
	EXPECT_EQ(b.worstSideIndex, 0);
}

TEST(SurfaceBudget, EmptyLevelBudgetIsZeroAndHasNoWorstSide)
{
	SideBudgetInput one = {};
	const LevelBudget none = ComputeLevelBudget(&one, 0);
	EXPECT_EQ(none.totalVertices, 0);
	EXPECT_EQ(none.sides, 0);
	EXPECT_EQ(none.maxPerSide, 0);
	EXPECT_EQ(none.worstSideIndex, -1);

	const LevelBudget nullptrCase = ComputeLevelBudget(0, 5);
	EXPECT_EQ(nullptrCase.totalVertices, 0);
	EXPECT_EQ(nullptrCase.worstSideIndex, -1);

	const LevelBudget negative = ComputeLevelBudget(&one, -1);
	EXPECT_EQ(negative.sides, 0);
}

TEST(SurfaceBudget, LevelBudgetDoesNotOverflowOnHugeMaps)
{
	// 65535 sides all at the heavy shape above: comfortably past 32-bit if it accumulated as int.
	static SideBudgetInput sides[4096];
	for (int i = 0; i < 4096; i++)
	{
		sides[i].numSegs = 64;
		sides[i].leftVertexSectors = 32;
		sides[i].rightVertexSectors = 32;
		sides[i].ffloorBlocks = 32;
		sides[i].twoSided = true;
	}
	const LevelBudget b = ComputeLevelBudget(sides, 4096);
	// per piece: 4 + 64 + 64 + 126 = 258; pieces: 35 -> 9030 per side
	EXPECT_EQ(b.maxPerSide, 9030);
	EXPECT_EQ(b.totalVertices, 9030LL * 4096LL);
}

// ---- ComputeBufferBytes ----------------------------------------------------

TEST(SurfaceBudget, BufferBytesIsVerticesTimesStride)
{
	EXPECT_EQ(ComputeBufferBytes(100, 20), 2000);
	EXPECT_EQ(ComputeBufferBytes(0, 20), 0);
}

TEST(SurfaceBudget, BufferBytesStaysWideForLargeCounts)
{
	// 200M vertices at 20 bytes is 4 GB -- must not wrap through a 32-bit intermediate.
	EXPECT_EQ(ComputeBufferBytes(200000000LL, 20), 4000000000LL);
}

TEST(SurfaceBudget, BufferBytesRejectsNonsense)
{
	EXPECT_EQ(ComputeBufferBytes(-1, 20), 0);
	EXPECT_EQ(ComputeBufferBytes(100, 0), 0);
	EXPECT_EQ(ComputeBufferBytes(100, -4), 0);
}
