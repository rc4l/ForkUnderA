// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Skips a missile's sub-steps that provably do nothing but move it; see README.md.
#ifndef ZX_SWEPTMOVE_H
#define ZX_SWEPTMOVE_H

#include "actor.h"
#include "features/swept-missiles/computation/sweptmove_compute.h"

struct FCheckPosition;
class AFastProjectile;

namespace zx { namespace sweptmove {

// [Doruk-Aksoy] Spawns that landed in the blockmap, so a fast projectile's skipped run ends where its trail might stand in its way.
extern unsigned int BlockmapSpawns;

// [Doruk-Aksoy] Whether this mover, in the sector it starts the tic in, may have its quiet sub-steps skipped.
bool Sweeps(AActor *mo, bool onWalkPlane);

// [Doruk-Aksoy] P_XYMovement's run: the last of steps 1..steps, each landing at start + Scale(move, k, steps), that are all quiet.
int QuietRunEndXY(AActor *mo, fixed_t startx, fixed_t starty, fixed_t xmove, fixed_t ymove, int steps);

// [Doruk-Aksoy] A fast projectile's run: the last of steps 1..count, each adding xfrac, yfrac and zfrac, that are all quiet.
int QuietRunEndFast(AActor *mo, fixed_t xfrac, fixed_t yfrac, fixed_t zfrac, int count);

// [Doruk-Aksoy] Plans a fast projectile's steps from index `from` of `count`: sweeps them while sweeps are left and it pays, else plans every step real.
void PlanFast(AActor *p, FastPlan &plan, int from, int count, fixed_t xfrac, fixed_t yfrac, fixed_t zfrac);

// [Doruk-Aksoy] Does what a quiet fast projectile step does without its P_TryMove, and says false when the trail it spawned landed in the blockmap, so the next step must be a real one.
bool EmulateQuietFastStep(AFastProjectile *p, FCheckPosition &tm, int &ripcount, int every, fixed_t xfrac, fixed_t yfrac, fixed_t zfrac, fixed_t &oldz);

}} // namespace zx::sweptmove

#endif
