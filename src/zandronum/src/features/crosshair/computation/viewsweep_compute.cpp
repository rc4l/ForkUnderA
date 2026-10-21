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
	const double kPi = 3.14159265358979323846;

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

double ComputeBamRadians(int64_t bam)
{
	const int64_t kTurn = INT64_C(0x100000000);
	const int64_t wrapped = bam & (kTurn - 1);
	const int64_t signedBam = wrapped >= (kTurn / 2) ? wrapped - kTurn : wrapped;
	return static_cast<double>(signedBam) * (kPi / 2147483648.0);
}

int ComputeGLViewportHeight(int screenblocks, int screenHeight)
{
	if (screenblocks >= 10) {
		return screenHeight;
	}
	return (screenblocks * screenHeight / 10) & ~7;
}

ReticleProjection ComputeGLReticleProjection(double fovDegrees, double aspect, int viewportWidth, int viewportHeight, double stretch)
{
	// [MobiusU9B] The same frame gl_scene.cpp picks: the field of view spans 4:3 unless the screen is taller (AspectTallerThanWide).
	const double fovRatio = aspect < 1.333 ? aspect : 1.333333;
	const double tanHalfY = std::tan(fovDegrees * kPi / 360.0) / fovRatio;
	// [MobiusU9B] Written as "not greater" so a NaN field of view or aspect is turned away too.
	if (!(tanHalfY > 0.0) || !(aspect > 0.0)) {
		return ReticleProjection{0.0, 0.0, stretch, false};
	}
	const double focalX = (viewportWidth / 2.0) / (aspect * tanHalfY);
	const double focalStretchedY = (viewportHeight / 2.0) / tanHalfY;
	return ReticleProjection{focalX, focalStretchedY * stretch, stretch, false};
}

SweepVector ComputeReticleDirection(double yaw, double pitchUp, double offsetX, double offsetY, const ReticleProjection &projection)
{
	// [MobiusU9B] Without a usable focal length an offset can't be turned into an angle, so the line stays on the view's centre.
	const double lateral = projection.focalX > 0.0 ? offsetX / projection.focalX : 0.0;
	const double drop = projection.focalY > 0.0 ? offsetY / projection.focalY : 0.0;
	double ahead = 1.0;
	double rise = std::tan(pitchUp) - drop;
	if (!projection.shear) {
		// [MobiusU9B] GL turns the camera through the pitch the stretched world needs (gl_scene.cpp), so the offset is taken in that turned frame.
		const double stretch = projection.stretch > 0.0 ? projection.stretch : 1.0;
		const double tilt = std::atan2(stretch * std::sin(pitchUp), std::cos(pitchUp));
		const double up = -drop * stretch;
		ahead = std::cos(tilt) - (std::sin(tilt) * up);
		rise = (std::sin(tilt) + (std::cos(tilt) * up)) / stretch;
	}
	// [MobiusU9B] Screen right is a quarter turn clockwise of the yaw.
	const double x = (std::cos(yaw) * ahead) + (std::sin(yaw) * lateral);
	const double y = (std::sin(yaw) * ahead) - (std::cos(yaw) * lateral);
	const double length = std::sqrt((x * x) + (y * y) + (rise * rise));
	return SweepVector{x / length, y / length, rise / length};
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
