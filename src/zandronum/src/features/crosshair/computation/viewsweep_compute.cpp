// [MobiusU9B] See viewsweep_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/crosshair/computation/viewsweep_compute.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace zx { namespace crosshair {

namespace
{
	// [MobiusU9B] Narrows [tEnter, tExit] to where the ray is between lo and hi on one axis, false once that is empty.
	bool ClipAxis(double origin, double dir, double lo, double hi, double &tEnter, double &tExit)
	{
		if (dir == 0.0) {
			return origin >= lo && origin <= hi;
		}
		double tLo = (lo - origin) / dir;
		double tHi = (hi - origin) / dir;
		if (tLo > tHi) {
			std::swap(tLo, tHi);
		}
		tEnter = std::max(tEnter, tLo);
		tExit = std::min(tExit, tHi);
		return tEnter <= tExit;
	}
}

SweepEntry ComputeRayBoxEntry(const SweepRay &ray, const SweepBox &box, double maxDistance)
{
	const SweepEntry miss{false, 0.0};
	// [MobiusU9B] Written as "not greater" so a NaN size is a miss too.
	if (!(box.radius > 0.0) || !(box.height > 0.0)) {
		return miss;
	}
	double tEnter = -std::numeric_limits<double>::infinity();
	double tExit = std::numeric_limits<double>::infinity();
	if (!ClipAxis(ray.x, ray.dx, box.x - box.radius, box.x + box.radius, tEnter, tExit)
		|| !ClipAxis(ray.y, ray.dy, box.y - box.radius, box.y + box.radius, tEnter, tExit)
		|| !ClipAxis(ray.z, ray.dz, box.z, box.z + box.height, tEnter, tExit)) {
		return miss;
	}
	// [MobiusU9B] Entering at or before the eye means the box is behind it or around it, and neither is what it looks at.
	if (tEnter <= 0.0 || tEnter > maxDistance) {
		return miss;
	}
	return SweepEntry{true, tEnter};
}

bool ComputeSweepConsiders(const SweepActor &actor)
{
	if (actor.isViewer || actor.invisible) {
		return false;
	}
	// [MobiusU9B] Puffs, blood and fog are out of the blockmap, and projectiles are too but still count unless they are the viewer's own.
	if (actor.isMissile) {
		return !actor.firedByViewer;
	}
	return actor.inBlockmap;
}

SweepKind ComputeSweepKind(const SweepActor &actor)
{
	if (actor.isPlayer) {
		return kSweepPlayer;
	}
	if (actor.isMonster) {
		return kSweepMonster;
	}
	if (actor.isMissile) {
		return kSweepProjectile;
	}
	if (actor.isItem) {
		return kSweepItem;
	}
	return kSweepDecoration;
}

int ComputeNearestSweepHit(const std::vector<SweepHit> &hits, bool measurableOnly, double maxDistance)
{
	int nearest = -1;
	for (size_t i = 0; i < hits.size(); ++i) {
		const SweepHit &hit = hits[i];
		if ((measurableOnly && !hit.measurable) || hit.distance > maxDistance) {
			continue;
		}
		if (nearest < 0 || hit.distance < hits[static_cast<size_t>(nearest)].distance) {
			nearest = static_cast<int>(i);
		}
	}
	return nearest;
}

bool ComputeSweepDue(int lastTic, int nowTic, bool sameViewer)
{
	return lastTic != nowTic || !sameViewer;
}

} } // namespace zx::crosshair
