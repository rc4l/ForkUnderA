// [MobiusU9B] See viewsweep_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/crosshair/computation/viewsweep_compute.h"

#include <algorithm>
#include <cmath>
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

	// [MobiusU9B] The span [tEnter, tExit] of the whole line that lies inside box, false when none does or the box has no size.
	bool ClipBox(const SweepRay &ray, const SweepBox &box, double &tEnter, double &tExit)
	{
		// [MobiusU9B] Written as "not greater" so a NaN size is a miss too.
		if (!(box.radius > 0.0) || !(box.height > 0.0)) {
			return false;
		}
		tEnter = -std::numeric_limits<double>::infinity();
		tExit = std::numeric_limits<double>::infinity();
		return ClipAxis(ray.x, ray.dx, box.x - box.radius, box.x + box.radius, tEnter, tExit)
			&& ClipAxis(ray.y, ray.dy, box.y - box.radius, box.y + box.radius, tEnter, tExit)
			&& ClipAxis(ray.z, ray.dz, box.z, box.z + box.height, tEnter, tExit);
	}
}

SweepEntry ComputeRayBoxEntry(const SweepRay &ray, const SweepBox &box, double maxDistance)
{
	double tEnter = 0.0;
	double tExit = 0.0;
	// [MobiusU9B] Entering at or before the eye means the box is behind it or around it, and neither is what it looks at.
	if (!ClipBox(ray, box, tEnter, tExit) || tEnter <= 0.0 || tEnter > maxDistance) {
		return SweepEntry{false, 0.0};
	}
	return SweepEntry{true, tEnter};
}

double ComputeRayBoxExit(const SweepRay &ray, const SweepBox &box)
{
	double tEnter = 0.0;
	double tExit = 0.0;
	// [MobiusU9B] Only a box around the eye has an exit, and a line with no direction never reaches it.
	if (!ClipBox(ray, box, tEnter, tExit) || tEnter > 0.0 || tExit < 0.0 || std::isinf(tExit)) {
		return 0.0;
	}
	return tExit;
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
