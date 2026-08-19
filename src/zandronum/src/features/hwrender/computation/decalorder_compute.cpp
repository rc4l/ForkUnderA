// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/decalorder_compute.h"

namespace zx { namespace hwrender {

const float kCoincidentDecalRadius = 64.f;
const float kDecalDistanceNudge    = 1.02f;

float ComputeSortDistance(float distSq, bool decal)
{
	return decal ? distSq * kDecalDistanceNudge : distSq;
}

static bool IsAdditive(const TranslucentDraw &d) { return d.blend == 2; }

static bool SameSpot(const TranslucentDraw &a, const TranslucentDraw &b)
{
	const float dx = a.cx - b.cx, dy = a.cy - b.cy, dz = a.cz - b.cz;
	return dx * dx + dy * dy + dz * dz < kCoincidentDecalRadius * kCoincidentDecalRadius;
}

bool ComputeDrawsBefore(const TranslucentDraw &a, const TranslucentDraw &b)
{
	// [rc4l] Two marks on the SAME SPOT are ordered by what they are, never by distance.
	//
	// One plasma bolt leaves two decals at one point: a black scorch and the additive glow that
	// belongs on top of it. They are paint on one wall, so their distances differ only by where each
	// quad's centre happens to fall -- 22546 against 22367 on the wall this was reported from, a
	// fifth of a percent -- and farthest-first therefore drew the glow first and painted the scorch
	// over it.
	if (a.decal && b.decal && SameSpot(a, b))
	{
		if (IsAdditive(a) != IsAdditive(b)) return !IsAdditive(a);
		return a.first < b.first;
	}

	if (a.distSq != b.distSq) return a.distSq > b.distSq;

	// [rc4l] At equal distance, additive still draws last, and then the buffer offset decides.
	//
	// std::sort is not stable, so two draws at one distance -- a decal and the one a template puts
	// underneath it -- traded places between frames and flickered through each other. Falling back to
	// creation order makes equal distances resolve the same way every frame.
	if (IsAdditive(a) != IsAdditive(b)) return !IsAdditive(a);
	return a.first < b.first;
}

} }   // namespace zx::hwrender
