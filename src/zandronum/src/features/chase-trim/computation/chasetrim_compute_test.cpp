// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/chase-trim/computation/chasetrim_compute.h"

#include <cstdint>

#include <gtest/gtest.h>

using namespace zx::chasetrim;

namespace {
const int64_t UNIT = 65536;	// [Doruk-Aksoy] One map unit in 48.16 fixed point.
const int64_t STEP = 24 * UNIT;	// [Doruk-Aksoy] The default MaxStepHeight.

BlockMemo Empty()
{
	BlockMemo memo = {};
	return memo;
}

Box Imp(int64_t x, int64_t y, int64_t z = 0)
{
	return Box{ x * UNIT, y * UNIT, z * UNIT, 20 * UNIT, 56 * UNIT };
}
}

// [Doruk-Aksoy] A near monster moves every call at its own speed and its phase resets.
TEST(ChaseTrim, NearMonstersMoveEveryCall)
{
	uint8_t state = 1;
	const FarStep step = ComputeFarChaseStep(false, state);
	EXPECT_FALSE(step.skip);
	EXPECT_EQ(step.scale, 1);
	EXPECT_EQ(state, 0);
}

// [Doruk-Aksoy] A far monster skips one call and walks twice as far the next, so it covers the same ground.
TEST(ChaseTrim, FarMonstersAlternateSkipAndDoubleStep)
{
	uint8_t state = 0;
	FarStep step = ComputeFarChaseStep(true, state);
	EXPECT_TRUE(step.skip);
	step = ComputeFarChaseStep(true, state);
	EXPECT_FALSE(step.skip);
	EXPECT_EQ(step.scale, 2);
	step = ComputeFarChaseStep(true, state);
	EXPECT_TRUE(step.skip);
}

// [Doruk-Aksoy] A hold walks single steps every call for its length, far or near, then the far rhythm returns.
TEST(ChaseTrim, FarHoldWalksSingleStepsFirst)
{
	uint8_t state = 1;
	ComputeFarHold(state, 2);
	EXPECT_EQ(state, 4);
	FarStep step = ComputeFarChaseStep(true, state);
	EXPECT_FALSE(step.skip);
	EXPECT_EQ(step.scale, 1);
	step = ComputeFarChaseStep(false, state);
	EXPECT_FALSE(step.skip);
	EXPECT_EQ(state, 0);
	step = ComputeFarChaseStep(true, state);
	EXPECT_TRUE(step.skip);
	ComputeFarHold(state, 500);
	EXPECT_EQ(state, 127 << 1);
	ComputeFarHold(state, -3);
	EXPECT_EQ(state, 0);
}

// [Doruk-Aksoy] Failed re-plans count up to 255 and a move resets them; a walker is jammed from `after` in a row.
TEST(ChaseTrim, JamCountsConsecutiveFailures)
{
	uint8_t count = 0;
	ComputeJamCount(count, true);
	ComputeJamCount(count, true);
	EXPECT_EQ(count, 2);
	EXPECT_FALSE(ComputeJammed(count, 3));
	ComputeJamCount(count, true);
	EXPECT_TRUE(ComputeJammed(count, 3));
	EXPECT_TRUE(ComputeJammed(0, 0));
	ComputeJamCount(count, false);
	EXPECT_EQ(count, 0);
	count = 255;
	ComputeJamCount(count, true);
	EXPECT_EQ(count, 255);
}

// [Doruk-Aksoy] The distance is Doom's octagonal approximation, whatever the signs.
TEST(ChaseTrim, AproxDistanceMatchesDoom)
{
	EXPECT_EQ(ComputeAproxDistance(300, 400), 300 + 400 - 150);
	EXPECT_EQ(ComputeAproxDistance(-400, 300), 300 + 400 - 150);
	EXPECT_EQ(ComputeAproxDistance(0, -50), 50);
}

// [Doruk-Aksoy] One viewer inside the threshold keeps a monster near; it is far only from every viewer.
TEST(ChaseTrim, FarMeansFarFromEveryViewer)
{
	const int64_t vx[] = { 0, 5000 * UNIT };
	const int64_t vy[] = { 0, 0 };
	EXPECT_TRUE(ComputeFarFromViewers(2500 * UNIT, 2500 * UNIT, vx, vy, 1, 2048 * UNIT));
	EXPECT_FALSE(ComputeFarFromViewers(4000 * UNIT, 0, vx, vy, 2, 2048 * UNIT));
	EXPECT_TRUE(ComputeFarFromViewers(2500 * UNIT, 0, vx, vy, 2, 2048 * UNIT));
}

// [Doruk-Aksoy] A threshold of 0 turns it off, and with no viewers nothing is far.
TEST(ChaseTrim, NoThresholdOrNoViewersIsNeverFar)
{
	const int64_t vx[] = { 0 };
	const int64_t vy[] = { 0 };
	EXPECT_FALSE(ComputeFarFromViewers(9000 * UNIT, 0, vx, vy, 1, 0));
	EXPECT_FALSE(ComputeFarFromViewers(9000 * UNIT, 0, vx, vy, 0, 2048 * UNIT));
}

// [Doruk-Aksoy] A count already running is kept; one a failed re-plan left below 0 restarts from the low 4 bits of the roll.
TEST(ChaseTrim, StuckMoveCountRestartsOnlyWhenSpent)
{
	EXPECT_EQ(ComputeStuckMoveCount(5, 200), 5);
	EXPECT_EQ(ComputeStuckMoveCount(0, 200), 0);
	EXPECT_EQ(ComputeStuckMoveCount(-1, 0xAB), 0xB);
	EXPECT_EQ(ComputeStuckMoveCount(-7, 0), 0);
}

// [Doruk-Aksoy] A neighbour whose box the step would overlap still blocks; one the step clears does not, on either axis.
TEST(ChaseTrim, StillBlocksWhereTheBoxesOverlap)
{
	const Box walker = Imp(0, 0);
	EXPECT_TRUE(ComputeStillBlocks(walker, STEP, 8 * UNIT, 0, Imp(45, 0), false));
	EXPECT_FALSE(ComputeStillBlocks(walker, STEP, 8 * UNIT, 0, Imp(45, 40), false));
	EXPECT_FALSE(ComputeStillBlocks(walker, STEP, 8 * UNIT, 0, Imp(45, -40), false));
	EXPECT_FALSE(ComputeStillBlocks(walker, STEP, -8 * UNIT, 0, Imp(-60, 0), false));
	EXPECT_FALSE(ComputeStillBlocks(walker, STEP, 8 * UNIT, 0, Imp(70, 0), false));
}

// [Doruk-Aksoy] Above, below or low enough to step onto never blocks, unless actors are infinitely tall.
TEST(ChaseTrim, StillBlocksOnlyWhereHeightsMeet)
{
	const Box walker = Imp(0, 0);
	EXPECT_FALSE(ComputeStillBlocks(walker, STEP, 8 * UNIT, 0, Imp(45, 0, 60), false));
	EXPECT_FALSE(ComputeStillBlocks(walker, STEP, 8 * UNIT, 0, Imp(45, 0, -60), false));
	const Box low = Box{ 45 * UNIT, 0, 0, 20 * UNIT, 16 * UNIT };
	EXPECT_FALSE(ComputeStillBlocks(walker, STEP, 8 * UNIT, 0, low, false));
	EXPECT_TRUE(ComputeStillBlocks(walker, STEP, 8 * UNIT, 0, low, true));
	EXPECT_TRUE(ComputeStillBlocks(walker, STEP, 8 * UNIT, 0, Imp(45, 0, 60), true));
}

// [Doruk-Aksoy] A step that takes an overlapping walker further from the blocker is allowed, as PIT_CheckThing's unblocking rule allows it.
TEST(ChaseTrim, StepsAwayAreNotBlocked)
{
	const Box walker = Imp(0, 0);
	EXPECT_FALSE(ComputeStillBlocks(walker, STEP, -8 * UNIT, 0, Imp(30, 0), false));
	EXPECT_TRUE(ComputeStillBlocks(walker, STEP, 8 * UNIT, 0, Imp(30, 0), false));
}

// [Doruk-Aksoy] Actors take slots, a blocker already held reuses its slot, and one past the last slot is not remembered.
TEST(ChaseTrim, MemoKeepsAsManyBlockersAsSlots)
{
	BlockMemo memo = Empty();
	EXPECT_EQ(ComputeMemoSlot(memo, 3), -1);
	for (int s = 0; s < kMemoSlots; s++)
		EXPECT_EQ(ComputeMemoRecordActor(memo, s, -1, 10), s);
	EXPECT_EQ(ComputeMemoRecordActor(memo, 7, -1, 10), -1);
	EXPECT_EQ(ComputeMemoSlot(memo, 7), -1);
	EXPECT_EQ(ComputeMemoRecordActor(memo, 7, 2, 10), 2);
	EXPECT_EQ(ComputeMemoSlot(memo, 7), 2);
	ComputeMemoForgetSlot(memo, 2);
	EXPECT_EQ(ComputeMemoSlot(memo, 2), -1);
	EXPECT_EQ(ComputeMemoSlot(memo, 7), -1);
	EXPECT_EQ(ComputeMemoRecordActor(memo, 6, -1, 10), 2);
	EXPECT_EQ(ComputeMemoSlot(memo, kMemoSlots - 1), kMemoSlots - 1);
}

// [Doruk-Aksoy] Wall directions hold only while the walker stands where they refused it, and a new spot starts over.
TEST(ChaseTrim, MemoLinesHoldWhereTheyWereSeen)
{
	BlockMemo memo = Empty();
	ComputeMemoRecordLines(memo, 0x05, 1, 2, 3, 7, 10);
	EXPECT_EQ(ComputeMemoLines(memo, 1, 2, 3, 7), 0x05);
	EXPECT_EQ(ComputeMemoLines(memo, 1, 2, 4, 7), 0);
	EXPECT_EQ(ComputeMemoLines(memo, 9, 2, 3, 7), 0);
	EXPECT_EQ(ComputeMemoLines(memo, 1, 9, 3, 7), 0);
	EXPECT_EQ(ComputeMemoLines(memo, 1, 2, 3, 8), 0);
	ComputeMemoRecordLines(memo, 0x10, 1, 2, 3, 7, 11);
	EXPECT_EQ(ComputeMemoLines(memo, 1, 2, 3, 7), 0x15);
	ComputeMemoRecordLines(memo, 0x02, 4, 2, 3, 7, 12);
	EXPECT_EQ(ComputeMemoLines(memo, 4, 2, 3, 7), 0x02);
	ComputeMemoRecordLines(memo, 0x04, 4, 5, 3, 7, 12);
	EXPECT_EQ(ComputeMemoLines(memo, 4, 5, 3, 7), 0x04);
	ComputeMemoRecordLines(memo, 0x08, 4, 5, 6, 7, 12);
	EXPECT_EQ(ComputeMemoLines(memo, 4, 5, 6, 7), 0x08);
	ComputeMemoRecordLines(memo, 0x01, 4, 5, 6, 9, 12);
	EXPECT_EQ(ComputeMemoLines(memo, 4, 5, 6, 9), 0x01);
}

// [Doruk-Aksoy] A memo ages out as a whole after kMemoMaxAge tics from its first entry.
TEST(ChaseTrim, MemoAgesOut)
{
	BlockMemo memo = Empty();
	EXPECT_FALSE(ComputeMemoRefresh(memo, 10, kMemoMaxAge));
	ComputeMemoRecordActor(memo, 0, -1, 10);
	ComputeMemoRecordLines(memo, 0x02, 1, 2, 3, 0, 20);
	EXPECT_EQ(memo.since, 10);
	EXPECT_TRUE(ComputeMemoRefresh(memo, 10 + kMemoMaxAge, kMemoMaxAge));
	EXPECT_FALSE(ComputeMemoRefresh(memo, 11 + kMemoMaxAge, kMemoMaxAge));
	EXPECT_EQ(ComputeMemoSlot(memo, 0), -1);
	EXPECT_EQ(ComputeMemoLines(memo, 1, 2, 3, 0), 0);
	ComputeMemoRecordLines(memo, 0x01, 1, 2, 3, 0, 99);
	EXPECT_EQ(memo.since, 99);
	EXPECT_TRUE(ComputeMemoRefresh(memo, 100, kMemoMaxAge));
}

// [Doruk-Aksoy] A step up to radius less one unit is one move; longer ones split as P_Move splits them, and a radius of a unit or less never splits.
TEST(ChaseTrim, MoveStepsMatchPMove)
{
	EXPECT_EQ(ComputeMoveSteps(8 * UNIT, 0, 20 * UNIT), 1);
	EXPECT_EQ(ComputeMoveSteps(19 * UNIT, 0, 20 * UNIT), 1);
	EXPECT_EQ(ComputeMoveSteps(20 * UNIT, 0, 20 * UNIT), 2);
	EXPECT_EQ(ComputeMoveSteps(0, -40 * UNIT, 20 * UNIT), 3);
	EXPECT_EQ(ComputeMoveSteps(-30 * UNIT, 10 * UNIT, 20 * UNIT), 2);
	EXPECT_EQ(ComputeMoveSteps(30 * UNIT, 0, UNIT), 1);
}

// [Doruk-Aksoy] A countdown waits that many calls, then lets the walker move.
TEST(ChaseTrim, JamWaitCountsDown)
{
	uint8_t countdown = 2;
	EXPECT_TRUE(ComputeJamWait(countdown));
	EXPECT_TRUE(ComputeJamWait(countdown));
	EXPECT_FALSE(ComputeJamWait(countdown));
	EXPECT_EQ(countdown, 0);
}

// [Doruk-Aksoy] A skipped call spends a step of movecount; at 0 or below it walks a single step so the re-plan and missile check come when they always did.
TEST(ChaseTrim, FarSkipKeepsMoveCount)
{
	FarStep step = { true, 1 };
	int movecount = 3;
	ComputeFarKeepCount(step, movecount);
	EXPECT_TRUE(step.skip);
	EXPECT_EQ(movecount, 2);
	movecount = 0;
	ComputeFarKeepCount(step, movecount);
	EXPECT_FALSE(step.skip);
	EXPECT_EQ(step.scale, 1);
	EXPECT_EQ(movecount, 0);
	step = FarStep{ false, 2 };
	movecount = 5;
	ComputeFarKeepCount(step, movecount);
	EXPECT_FALSE(step.skip);
	EXPECT_EQ(step.scale, 2);
	EXPECT_EQ(movecount, 5);
}
