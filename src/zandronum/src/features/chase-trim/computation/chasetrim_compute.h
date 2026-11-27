// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#pragma once

#include <cstdint>

namespace zx { namespace chasetrim {

// [Doruk-Aksoy] What one chase call of a monster does with its movement.
struct FarStep
{
	bool skip;	// [Doruk-Aksoy] Leave movement out of this call.
	int scale;	// [Doruk-Aksoy] Step length in multiples of the monster's Speed.
};

const int kFarHoldCalls = 8;	// [Doruk-Aksoy] Chase calls a far monster walks single steps after a double step is refused, so it squeezes through tight spaces as before.

// [Doruk-Aksoy] A far monster moves every other chase call with a double step (state bit 0 is the phase); while its hold (bits 1-7) lasts, or near, it moves every call and the hold counts down.
FarStep ComputeFarChaseStep(bool far, uint8_t &state);

// [Doruk-Aksoy] A skipped call counts movecount down as a walked step would; one that would re-plan (movecount 0 or less) is not skipped and walks a single step.
void ComputeFarKeepCount(FarStep &step, int &movecount);

// [Doruk-Aksoy] Starts a hold of `calls` single-step chase calls, clearing the phase.
void ComputeFarHold(uint8_t &state, int calls);

// [Doruk-Aksoy] Counts consecutive re-plans that failed every direction, saturating, and back to 0 once the walker moves.
void ComputeJamCount(uint8_t &count, bool failedEverywhere);

// [Doruk-Aksoy] Whether a walker is jammed: `after` re-plans in a row failed every direction (0 or less: always).
bool ComputeJammed(uint8_t count, int after);

// [Doruk-Aksoy] P_AproxDistance on raw fixed-point deltas.
int64_t ComputeAproxDistance(int64_t dx, int64_t dy);

// [Doruk-Aksoy] Whether (x, y) is at least threshold from every viewer; a threshold of 0 or less, or no viewers, is never far.
bool ComputeFarFromViewers(int64_t x, int64_t y, const int64_t *viewerX, const int64_t *viewerY, int count, int64_t threshold);

// [Doruk-Aksoy] After a re-plan found no direction, a movecount below 0 restarts from random (0-255) like a walk would, so the missile check at 0 comes round again.
int ComputeStuckMoveCount(int movecount, int random);

// [Doruk-Aksoy] How many moves P_Move splits a step of (dx, dy) into, so none is longer than radius less one unit.
int ComputeMoveSteps(int64_t dx, int64_t dy, int64_t radius);

// [Doruk-Aksoy] Counts a jammed walker's wait down; returns whether this call waits.
bool ComputeJamWait(uint8_t &countdown);

// [Doruk-Aksoy] An actor's box in raw fixed point.
struct Box
{
	int64_t x, y, z, radius, height;
};

// [Doruk-Aksoy] Whether blocker still refuses a walker stepping to (destX, destY) as PIT_CheckThing would: boxes overlap, heights overlap unless infinitely tall, too tall to step onto, and the step does not take the walker away from it.
bool ComputeStillBlocks(const Box &walker, int64_t maxStep, int64_t destX, int64_t destY, const Box &blocker, bool infinitelyTall);

const int kMemoSlots = 6;	// [Doruk-Aksoy] Distinct blockers one walker remembers; 98% of stuck walkers have no more.
const int kMemoDirs = 8;	// [Doruk-Aksoy] Walking directions, DI_EAST to DI_SOUTHEAST.
const int kMemoMaxAge = 35;	// [Doruk-Aksoy] Tics a memo lasts before every direction is tried again.

// [Doruk-Aksoy] Directions a walker failed to move in: which remembered actor blocked each, and which a wall or ledge refused from where it stood.
struct BlockMemo
{
	int64_t x, y, z;	// [Doruk-Aksoy] Where the walker stood when walls refused it.
	int32_t since;		// [Doruk-Aksoy] Tic the memo started.
	uint32_t epoch;		// [Doruk-Aksoy] World changes counted when walls refused it; any sector, line or polyobject change since forgets them.
	uint32_t dirSlots;	// [Doruk-Aksoy] Four bits a direction: 0 none, else the blocking slot + 1.
	uint8_t lines;		// [Doruk-Aksoy] Directions a wall or ledge refused.
};

// [Doruk-Aksoy] The slot remembered as blocking dir, or -1.
int ComputeMemoSlot(const BlockMemo &memo, int dir);

// [Doruk-Aksoy] Clears a memo started more than maxAge tics ago; returns whether anything is still remembered.
bool ComputeMemoRefresh(BlockMemo &memo, int now, int maxAge);

// [Doruk-Aksoy] Directions walls refused, as long as the walker still stands where they did and the world has not changed.
uint8_t ComputeMemoLines(const BlockMemo &memo, int64_t x, int64_t y, int64_t z, uint32_t epoch);

// [Doruk-Aksoy] Forgets every direction slot held.
void ComputeMemoForgetSlot(BlockMemo &memo, int slot);

// [Doruk-Aksoy] Remembers dir as blocked by the actor already in slot `same` (or -1 for a new one); returns the slot used, or -1 when all are taken.
int ComputeMemoRecordActor(BlockMemo &memo, int dir, int same, int now);

// [Doruk-Aksoy] Remembers the directions walls refused from where the walker stands, in this world.
void ComputeMemoRecordLines(BlockMemo &memo, uint8_t lines, int64_t x, int64_t y, int64_t z, uint32_t epoch, int now);

} } // namespace zx::chasetrim
