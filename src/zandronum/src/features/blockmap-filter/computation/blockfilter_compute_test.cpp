// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for the blockmap candidate filter's distance test.
#include "gtest/gtest.h"
#include "features/blockmap-filter/computation/blockfilter_compute.h"

namespace
{
const int64_t U = 65536;

// [Doruk-Aksoy] PIT_CheckThing's own test, written as it is there, for comparing against.
bool Reference(int64_t cx, int64_t cy, int64_t cr, int64_t car, int64_t mx, int64_t my, int64_t mr, bool missile)
{
	const int64_t blockdist = (missile ? car : cr) + mr;
	const int64_t dx = cx - mx, dy = cy - my;
	return (dx < 0 ? -dx : dx) >= blockdist || (dy < 0 ? -dy : dy) >= blockdist;
}

// [Doruk-Aksoy] Touching boxes stay candidates and boxes exactly a radius sum apart are missed, on either side and either axis.
TEST(ComputeBlockCandidateMissed, EdgeIsMissedInsideIsKept)
{
	EXPECT_TRUE(ComputeBlockCandidateMissed(36 * U, 0, 20 * U, 20 * U, 0, 0, 16 * U, false));
	EXPECT_FALSE(ComputeBlockCandidateMissed(36 * U - 1, 0, 20 * U, 20 * U, 0, 0, 16 * U, false));
	EXPECT_TRUE(ComputeBlockCandidateMissed(-36 * U, 0, 20 * U, 20 * U, 0, 0, 16 * U, false));
	EXPECT_FALSE(ComputeBlockCandidateMissed(0, -36 * U + 1, 20 * U, 20 * U, 0, 0, 16 * U, false));
	EXPECT_TRUE(ComputeBlockCandidateMissed(0, 36 * U, 20 * U, 20 * U, 0, 0, 16 * U, false));
}

// [Doruk-Aksoy] A missile measures against the candidate's attack radius and anything else against its radius.
TEST(ComputeBlockCandidateMissed, MissileUsesAttackRadius)
{
	EXPECT_FALSE(ComputeBlockCandidateMissed(50 * U, 0, 20 * U, 40 * U, 0, 0, 16 * U, true));
	EXPECT_TRUE(ComputeBlockCandidateMissed(50 * U, 0, 20 * U, 40 * U, 0, 0, 16 * U, false));
	EXPECT_TRUE(ComputeBlockCandidateMissed(30 * U, 0, 20 * U, 10 * U, 0, 0, 16 * U, true));
}

// [Doruk-Aksoy] Far 48.16 coordinates and a grid of offsets give PIT_CheckThing's answer.
TEST(ComputeBlockCandidateMissed, MatchesPitCheckThingEverywhere)
{
	const int64_t far = (int64_t)1 << 40;
	for (int64_t base : { (int64_t)0, far, -far })
		for (int64_t dx = -60; dx <= 60; dx += 7)
			for (int64_t dy = -60; dy <= 60; dy += 11)
				for (bool missile : { false, true })
				{
					const int64_t cx = base + dx * U, cy = base - dy * U;
					EXPECT_EQ(Reference(cx, cy, 20 * U, 32 * U, base, base, 16 * U, missile),
						ComputeBlockCandidateMissed(cx, cy, 20 * U, 32 * U, base, base, 16 * U, missile));
				}
}
}
