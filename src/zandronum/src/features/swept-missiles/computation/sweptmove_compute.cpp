// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/swept-missiles/computation/sweptmove_compute.h"

namespace zx { namespace sweptmove {

bool ComputeSweepsMover(const MoverAsk &m)
{
	return m.missile && !m.player && !m.noClip && !m.stepMissile && !m.blasted && !m.hugger && !m.cantLeaveFloorpic && !m.onWalkPlane && !m.wallRun;
}

bool ComputeSweepPays(int steps)
{
	return steps >= 4;
}

bool ComputeSweepsSector(const SectorAsk &s)
{
	return s.flatFloor && s.flatCeiling && !s.has3DFloors && !s.hasHeightSec && !s.underwater;
}

bool ComputeStepMeetsThing(bool canBeHit, int64_t thingX, int64_t thingY, int64_t hitRadius, int64_t stepX, int64_t stepY, int64_t moverRadius)
{
	if (!canBeHit) return false;
	const int64_t blockdist = hitRadius + moverRadius;
	const int64_t dx = thingX - stepX, dy = thingY - stepY;
	return (dx < 0 ? -dx : dx) < blockdist && (dy < 0 ? -dy : dy) < blockdist;
}

bool ComputeStepClearsPlanes(int64_t zBefore, int64_t zAfter, int64_t height, int64_t floorZ, int64_t ceilingZ)
{
	return zBefore >= floorZ && zBefore + height <= ceilingZ && zAfter > floorZ && zAfter + height <= ceilingZ;
}

int ComputeFirstUnclearStep(const int64_t *zs, int steps, int64_t height, int64_t floorZ, int64_t ceilingZ)
{
	for (int k = 1; k <= steps; k++)
		if (!ComputeStepClearsPlanes(zs[k - 1], zs[k], height, floorZ, ceilingZ)) return k;
	return steps + 1;
}

int ComputeFirstStepMeetingThing(const int64_t *xs, const int64_t *ys, int before, int64_t thingX, int64_t thingY, int64_t hitRadius, int64_t moverRadius)
{
	for (int k = 1; k < before; k++)
		if (ComputeStepMeetsThing(true, thingX, thingY, hitRadius, xs[k], ys[k], moverRadius)) return k;
	return before;
}

void ComputeFastPlan(FastPlan &plan, int from, int remaining, int quiet)
{
	plan.emulateTo = quiet >= 2 ? from + quiet - 1 : from;
	plan.loudAt = quiet < remaining ? from + quiet : -1;
}

RipStep ComputeRipStep(int &ripcount, int every)
{
	RipStep r;
	r.clearRipped = --ripcount <= 0;
	r.effect = ripcount <= 0;
	if (r.effect) ripcount = every;
	return r;
}

}} // namespace zx::sweptmove
