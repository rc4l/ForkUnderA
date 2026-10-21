// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/surfacebudget_compute.h"

namespace zx { namespace levelmesh {

int ComputePieceVertexBudget(int numSegs, int leftVertexSectors, int rightVertexSectors)
{
	// [rc4l] Defensive clamps: a malformed map can hand us zero or negative counts, and a budget
	// must never come out below the four corners or a range would be too small to hold its own quad.
	if (numSegs < 1) numSegs = 1;
	if (leftVertexSectors < 0) leftVertexSectors = 0;
	if (rightVertexSectors < 0) rightVertexSectors = 0;

	const int corners = 4;
	const int leftSplits = 2 * leftVertexSectors;   // floor + ceiling per touching sector
	const int rightSplits = 2 * rightVertexSectors;
	const int spanSplits = 2 * (numSegs - 1);       // upper edge and lower edge

	return corners + leftSplits + rightSplits + spanSplits;
}

int ComputeSidePieceCount(bool twoSided, int ffloorBlocks)
{
	if (ffloorBlocks < 0) ffloorBlocks = 0;
	return (twoSided ? 3 : 1) + ffloorBlocks;
}

int ComputeSideVertexBudget(const SideBudgetInput &in)
{
	const int pieces = ComputeSidePieceCount(in.twoSided, in.ffloorBlocks);
	const int perPiece = ComputePieceVertexBudget(in.numSegs, in.leftVertexSectors, in.rightVertexSectors);
	return pieces * perPiece;
}

LevelBudget ComputeLevelBudget(const SideBudgetInput *sides, int count)
{
	LevelBudget out = { 0, 0, 0, -1 };
	if (sides == 0 || count <= 0) return out;

	for (int i = 0; i < count; i++)
	{
		const int budget = ComputeSideVertexBudget(sides[i]);
		out.totalVertices += budget;
		if (budget > out.maxPerSide)
		{
			out.maxPerSide = budget;
			out.worstSideIndex = i;
		}
	}
	out.sides = count;
	return out;
}

long long ComputeBufferBytes(long long vertices, int strideBytes)
{
	if (vertices < 0 || strideBytes <= 0) return 0;
	return vertices * (long long)strideBytes;
}

}} // namespace zx::levelmesh
