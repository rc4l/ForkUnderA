// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/flatmesh_compute.h"

#include <math.h>

namespace zx { namespace levelmesh {

bool ComputeFlatWindingReversed(bool viewedFromBelow)
{
	return viewedFromBelow;
}

bool ComputeFlatNormalFlipped(bool viewedFromBelow, float planeUp)
{
	if (planeUp > 0.f) return viewedFromBelow;
	if (planeUp < 0.f) return !viewedFromBelow;
	return false;   // vertical plane: not a flat, and its sign means nothing
}

int ComputeSurfaceBlendMode(bool additive, float alpha)
{
	if (additive) return 2;
	// One 8-bit step: Doom stores translucency as 0..255, so 255/255 must classify as opaque while
	// 254/255 must not.
	return (alpha < 1.f - 1.f / 256.f) ? 1 : 0;
}

int ComputeWallBlendMode(bool inTranslucentList, bool additive, float alpha)
{
	if (additive) return 2;
	if (inTranslucentList) return 1;
	return ComputeSurfaceBlendMode(false, alpha);
}

int ComputeStyleBlendMode(bool shadow, bool additive, float alpha)
{
	if (shadow) return 3;
	if (additive) return 2;
	return ComputeSurfaceBlendMode(false, alpha);
}

float ComputeTriangleWindingZ(float ax, float ay, float bx, float by, float cx, float cy)
{
	return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

bool ComputeCoplanarOverlap(const MeshBox &a, const MeshBox &b, float eps)
{
	// Separated on any axis: not touching at all.
	if (a.x1 < b.x0 - eps || b.x1 < a.x0 - eps) return false;
	if (a.y1 < b.y0 - eps || b.y1 < a.y0 - eps) return false;
	if (a.z1 < b.z0 - eps || b.z1 < a.z0 - eps) return false;

	// Coplanar: both must be flat in the same axis. (The overlap test above has already established
	// they share a value on that axis, to within eps.)
	const bool flatX = (a.x1 - a.x0) < eps && (b.x1 - b.x0) < eps;
	const bool flatY = (a.y1 - a.y0) < eps && (b.y1 - b.y0) < eps;
	const bool flatZ = (a.z1 - a.z0) < eps && (b.z1 - b.z0) < eps;
	if (!flatX && !flatY && !flatZ) return false;

	// And the overlap must have area, not merely a shared edge.
	const float ox = (a.x1 < b.x1 ? a.x1 : b.x1) - (a.x0 > b.x0 ? a.x0 : b.x0);
	const float oy = (a.y1 < b.y1 ? a.y1 : b.y1) - (a.y0 > b.y0 ? a.y0 : b.y0);
	const float oz = (a.z1 < b.z1 ? a.z1 : b.z1) - (a.z0 > b.z0 ? a.z0 : b.z0);
	int wide = 0;
	if (ox > eps) wide++;
	if (oy > eps) wide++;
	if (oz > eps) wide++;
	return wide >= 2;
}

bool ComputeWindingConsistent(int fromAbovePositive, int fromAboveNegative,
                              int fromBelowPositive, int fromBelowNegative)
{
	// Each group must be unanimous.
	if (fromAbovePositive > 0 && fromAboveNegative > 0) return false;
	if (fromBelowPositive > 0 && fromBelowNegative > 0) return false;

	const bool haveAbove = (fromAbovePositive + fromAboveNegative) > 0;
	const bool haveBelow = (fromBelowPositive + fromBelowNegative) > 0;
	// An absent group cannot disagree with anything.
	if (!haveAbove || !haveBelow) return true;

	// And the two must be opposite, or one cull mode cannot keep both.
	return (fromAbovePositive > 0) != (fromBelowPositive > 0);
}

PlaneUVTransform ComputeIdentityPlaneUV()
{
	PlaneUVTransform t;
	t.xoffs = t.yoffs = 0.f;
	t.xscale = t.yscale = 1.f;
	t.angleDegrees = 0.f;
	t.texWidth = t.texHeight = 64.f;
	t.hasCanvas = false;
	return t;
}

void ComputePlaneUV(float px, float py, const PlaneUVTransform &t, float &u, float &v)
{
	// The untransformed mapping: one texture per 64 map units, V running the other way.
	float uu = px / 64.f;
	float vv = -py / 64.f;

	// A zero-sized texture would divide by zero; treat it as the 64x64 default rather than produce
	// infinities that propagate silently into the vertex buffer.
	const float tw = (t.texWidth  > 0.f) ? t.texWidth  : 64.f;
	const float th = (t.texHeight > 0.f) ? t.texHeight : 64.f;

	// 1. Rotate. GL negates the plane's angle before handing it to the matrix.
	if (t.angleDegrees != 0.f)
	{
		const float a = -t.angleDegrees * 3.14159265358979323846f / 180.f;
		const float ca = cosf(a), sa = sinf(a);
		const float ru = uu * ca - vv * sa;
		const float rv = uu * sa + vv * ca;
		uu = ru; vv = rv;
	}

	// 2. Correct for the texture's real size, which the /64 above assumed was 64.
	uu *= 64.f / tw;
	vv *= 64.f / th;

	// 3. Offset, in texture widths. This is the term a scrolling floor animates.
	uu += t.xoffs / tw;
	vv += t.yoffs / th;

	// 4. The plane's own scale. A canvas texture is stored upside down, so its V scale is negated.
	uu *= t.xscale;
	vv *= t.hasCanvas ? -t.yscale : t.yscale;

	u = uu;
	v = vv;
}

}} // namespace zx::levelmesh
