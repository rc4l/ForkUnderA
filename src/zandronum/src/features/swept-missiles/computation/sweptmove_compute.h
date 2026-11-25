// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure rules for skipping a missile's sub-steps that provably do nothing but move it.
#ifndef ZX_SWEPTMOVE_COMPUTE_H
#define ZX_SWEPTMOVE_COMPUTE_H

#include <cstdint>

namespace zx { namespace sweptmove {

// [Doruk-Aksoy] The mover's flags and state that decide whether a quiet sub-step's P_TryMove depends on nothing but the sector it stays in.
struct MoverAsk
{
	bool missile, player, noClip, stepMissile, blasted, hugger, cantLeaveFloorpic, onWalkPlane, wallRun;
};

// [Doruk-Aksoy] Whether a mover's quiet sub-steps may be skipped: a plain missile, not a player, not clipping-free, not climbing or hugging planes, not blasted, not walking a slope and without wall running.
bool ComputeSweepsMover(const MoverAsk &m);

// [Doruk-Aksoy] Whether a move of this many sub-steps is worth a sweep: one costs about one sub-step's walk, so it pays only when it can skip two or more (measured: a loss at 2 steps, even at 3).
bool ComputeSweepPays(int steps);

// [Doruk-Aksoy] What of the mover's sector would make a quiet sub-step's outcome vary with its x and y.
struct SectorAsk
{
	bool flatFloor, flatCeiling, has3DFloors, hasHeightSec, underwater;
};

// [Doruk-Aksoy] Whether a sector gives every point the same floor, ceiling and water: flat planes, no 3D floors, no deep water and not underwater.
bool ComputeSweepsSector(const SectorAsk &s);

// [Doruk-Aksoy] PIT_CheckThing's opening at a sub-step: whether the thing can be hit and its hit box overlaps the mover's box there, the only case that does anything.
bool ComputeStepMeetsThing(bool canBeHit, int64_t thingX, int64_t thingY, int64_t hitRadius, int64_t stepX, int64_t stepY, int64_t moverRadius);

// [Doruk-Aksoy] Whether a sub-step from height zBefore to zAfter keeps a mover of this height between a flat floor and ceiling, so neither the move nor the floor and ceiling checks after it stop it.
bool ComputeStepClearsPlanes(int64_t zBefore, int64_t zAfter, int64_t height, int64_t floorZ, int64_t ceilingZ);

// [Doruk-Aksoy] The first of steps 1..steps whose heights (zs[k-1] before, zs[k] after) do not clear the planes, or steps + 1.
int ComputeFirstUnclearStep(const int64_t *zs, int steps, int64_t height, int64_t floorZ, int64_t ceilingZ);

// [Doruk-Aksoy] The first of steps 1..before-1, landing at (xs[k], ys[k]), whose box meets a hittable thing, or before.
int ComputeFirstStepMeetingThing(const int64_t *xs, const int64_t *ys, int before, int64_t thingX, int64_t thingY, int64_t hitRadius, int64_t moverRadius);

// [Doruk-Aksoy] A fast projectile's plan from step index `from`: emulate the steps before emulateTo, run the rest for real, and plan again right after the loud step at loudAt (-1 when none is known).
struct FastPlan
{
	int emulateTo, loudAt, sweepsLeft;
};

// [Doruk-Aksoy] The plan after a sweep found `quiet` leading quiet steps among the `remaining` from index `from`: all but the last of a run of two or more are emulated.
void ComputeFastPlan(FastPlan &plan, int from, int remaining, int quiet);

// [Doruk-Aksoy] A fast projectile's per-step rip and trail count: decrements it, says whether the step clears the last ripped actor before moving and whether its trail is spawned after, and rearms it then.
struct RipStep
{
	bool clearRipped, effect;
};
RipStep ComputeRipStep(int &ripcount, int every);

}} // namespace zx::sweptmove

#endif
