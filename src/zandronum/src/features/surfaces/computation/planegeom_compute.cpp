// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/computation/planegeom_compute.h"

#include <math.h>

namespace zx { namespace surfaces {

float ComputePlaneHeightAt(const SurfacePlane &p, float x, float y)
{
	// [rc4l] A vertical plane has no height anywhere, and dividing by its c is how a sector with a
	// malformed slope takes the whole level's geometry to infinity.
	if (p.c == 0.f) return 0.f;
	return (-p.d - p.a * x - p.b * y) / p.c;
}

bool ComputePlaneIsSloped(const SurfacePlane &p)
{
	return p.a != 0.f || p.b != 0.f;
}

bool ComputePlaneFacesViewer(const SurfacePlane &p, float x, float y, float viewerZ)
{
	if (p.c == 0.f) return false;
	const float h = ComputePlaneHeightAt(p, x, y);
	// c negative is a floor in ZDoom's normalisation, and a floor is seen from above it.
	return (p.c < 0.f) ? (viewerZ > h) : (viewerZ < h);
}

void ComputePlaneNormal(const SurfacePlane &p, bool seenFromBelow, float outNormal[3])
{
	// Mesh space is (x, z-up, y): a plane's (a, b, c) becomes (a, c, b) here.
	const float len = sqrtf(p.a * p.a + p.b * p.b + p.c * p.c);
	if (len <= 0.0001f)
	{
		outNormal[0] = 0.f; outNormal[1] = 1.f; outNormal[2] = 0.f;
		return;
	}
	const float sign = seenFromBelow ? -1.f : 1.f;
	// [rc4l] Turned to face the side the surface is SEEN from, not the way its plane points.
	//
	// A 3D floor's walkable top is the control sector's CEILING plane, so its normal points down
	// while the surface is looked at from above -- and a rule written as "floors face up" culls
	// exactly those, or lights them from the wrong side. The caller says which side it is seen from
	// because only the caller knows.
	outNormal[0] = sign * p.a / len;
	outNormal[1] = sign * p.c / len;
	outNormal[2] = sign * p.b / len;
}

}} // namespace zx::surfaces
