// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/swept-missiles/computation/sweptmove_compute.h"
#include <gtest/gtest.h>

using namespace zx::sweptmove;

namespace {
const int64_t U = 1 << 16;

TEST(ComputeSweepsMover, OnlyAPlainMissile)
{
	MoverAsk m = { true, false, false, false, false, false, false, false, false };
	EXPECT_TRUE(ComputeSweepsMover(m));
	MoverAsk off[] = {
		{ false, false, false, false, false, false, false, false, false },
		{ true, true, false, false, false, false, false, false, false },
		{ true, false, true, false, false, false, false, false, false },
		{ true, false, false, true, false, false, false, false, false },
		{ true, false, false, false, true, false, false, false, false },
		{ true, false, false, false, false, true, false, false, false },
		{ true, false, false, false, false, false, true, false, false },
		{ true, false, false, false, false, false, false, true, false },
		{ true, false, false, false, false, false, false, false, true },
	};
	for (const MoverAsk &o : off) EXPECT_FALSE(ComputeSweepsMover(o));
}

TEST(ComputeSweepPays, FromFourSteps)
{
	EXPECT_FALSE(ComputeSweepPays(1));
	EXPECT_FALSE(ComputeSweepPays(3));
	EXPECT_TRUE(ComputeSweepPays(4));
	EXPECT_TRUE(ComputeSweepPays(64));
}

TEST(ComputeSweepsSector, OnlyAPlainSector)
{
	EXPECT_TRUE(ComputeSweepsSector({ true, true, false, false, false }));
	EXPECT_FALSE(ComputeSweepsSector({ false, true, false, false, false }));
	EXPECT_FALSE(ComputeSweepsSector({ true, false, false, false, false }));
	EXPECT_FALSE(ComputeSweepsSector({ true, true, true, false, false }));
	EXPECT_FALSE(ComputeSweepsSector({ true, true, false, true, false }));
	EXPECT_FALSE(ComputeSweepsSector({ true, true, false, false, true }));
}

TEST(ComputeStepMeetsThing, MirrorsPitCheckThingsOverlap)
{
	// A 20-unit thing and a 6-unit missile meet when both axes are under 26 units apart.
	EXPECT_TRUE(ComputeStepMeetsThing(true, 0, 0, 20 * U, 25 * U, -25 * U, 6 * U));
	EXPECT_FALSE(ComputeStepMeetsThing(true, 0, 0, 20 * U, 26 * U, 0, 6 * U));
	EXPECT_FALSE(ComputeStepMeetsThing(true, 0, 0, 20 * U, 0, -26 * U, 6 * U));
	EXPECT_FALSE(ComputeStepMeetsThing(false, 0, 0, 20 * U, 0, 0, 6 * U));
	EXPECT_TRUE(ComputeStepMeetsThing(true, 0, 0, 20 * U, 0, 0, 0));
}

TEST(ComputeStepClearsPlanes, BetweenFloorAndCeiling)
{
	EXPECT_TRUE(ComputeStepClearsPlanes(10 * U, 12 * U, 8 * U, 0, 20 * U));
	EXPECT_TRUE(ComputeStepClearsPlanes(0, 1, 8 * U, 0, 20 * U));
	EXPECT_FALSE(ComputeStepClearsPlanes(-1, 5 * U, 8 * U, 0, 20 * U));
	EXPECT_FALSE(ComputeStepClearsPlanes(13 * U, 5 * U, 8 * U, 0, 20 * U));
	EXPECT_FALSE(ComputeStepClearsPlanes(5 * U, 0, 8 * U, 0, 20 * U));
	EXPECT_FALSE(ComputeStepClearsPlanes(5 * U, 12 * U + 1, 8 * U, 0, 20 * U));
}

TEST(ComputeFirstUnclearStep, StopsAtTheFirstStepOutOfThePlanes)
{
	const int64_t rising[] = { 0, 4 * U, 8 * U, 12 * U };
	EXPECT_EQ(ComputeFirstUnclearStep(rising, 3, 8 * U, -1, 20 * U), 4);
	EXPECT_EQ(ComputeFirstUnclearStep(rising, 3, 8 * U, -1, 18 * U), 3);
	EXPECT_EQ(ComputeFirstUnclearStep(rising, 3, 8 * U, 1, 20 * U), 1);
	EXPECT_EQ(ComputeFirstUnclearStep(rising, 0, 8 * U, 0, 20 * U), 1);
}

TEST(ComputeFirstStepMeetingThing, FirstOverlapOrBefore)
{
	const int64_t xs[] = { 0, 10 * U, 20 * U, 30 * U, 40 * U };
	const int64_t ys[] = { 0, 0, 0, 0, 0 };
	// A 10-unit thing at x 36 meets a 6-unit missile from x 21 on, so at step 3 first.
	EXPECT_EQ(ComputeFirstStepMeetingThing(xs, ys, 5, 36 * U, 0, 10 * U, 6 * U), 3);
	EXPECT_EQ(ComputeFirstStepMeetingThing(xs, ys, 3, 36 * U, 0, 10 * U, 6 * U), 3);
	EXPECT_EQ(ComputeFirstStepMeetingThing(xs, ys, 5, 36 * U, 17 * U, 10 * U, 6 * U), 5);
}

TEST(ComputeFastPlan, EmulatesAllButTheLastQuietStep)
{
	FastPlan p = { 0, 0, 3 };
	ComputeFastPlan(p, 0, 16, 16);
	EXPECT_EQ(p.emulateTo, 15);
	EXPECT_EQ(p.loudAt, -1);
	ComputeFastPlan(p, 0, 16, 5);
	EXPECT_EQ(p.emulateTo, 4);
	EXPECT_EQ(p.loudAt, 5);
	ComputeFastPlan(p, 6, 10, 1);
	EXPECT_EQ(p.emulateTo, 6);
	EXPECT_EQ(p.loudAt, 7);
	ComputeFastPlan(p, 6, 10, 0);
	EXPECT_EQ(p.emulateTo, 6);
	EXPECT_EQ(p.loudAt, 6);
	EXPECT_EQ(p.sweepsLeft, 3);
}

TEST(ComputeRipStep, MatchesTheFastProjectileLoop)
{
	int ripcount = 2;
	RipStep a = ComputeRipStep(ripcount, 2);
	EXPECT_FALSE(a.clearRipped);
	EXPECT_FALSE(a.effect);
	EXPECT_EQ(ripcount, 1);
	RipStep b = ComputeRipStep(ripcount, 2);
	EXPECT_TRUE(b.clearRipped);
	EXPECT_TRUE(b.effect);
	EXPECT_EQ(ripcount, 2);
	int every1 = 1;
	RipStep c = ComputeRipStep(every1, 1);
	EXPECT_TRUE(c.clearRipped && c.effect);
	EXPECT_EQ(every1, 1);
}
}
