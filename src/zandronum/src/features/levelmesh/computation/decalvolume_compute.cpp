// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/decalvolume_compute.h"

#include <cmath>

namespace zx { namespace levelmesh {

namespace {

float Dot3(const float a[3], const float b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float Length3(const float a[3])
{
	return std::sqrt(Dot3(a, a));
}

} // namespace

float ComputeDecalBoxDepth(float halfW, float halfH)
{
	const float reach = (halfW > halfH) ? halfW : halfH;
	return (reach > 24.f) ? reach : 24.f;
}

bool ComputeDecalBasis(const float axisU[3], const float axisV[3], const float axisN[3],
                       float halfW, float halfH, float halfDepth, DecalFrame &out)
{
	if (!(halfW > 0.f) || !(halfH > 0.f) || !(halfDepth > 0.f)) return false;
	for (int i = 0; i < 3; i++)
	{
		out.u[i] = axisU[i] / halfW;
		out.v[i] = axisV[i] / halfH;
		out.n[i] = axisN[i] / halfDepth;
	}
	return true;
}

bool ComputeWallDecalAxes(float dx, float dy, bool backSide, float along[2], float normal[2])
{
	const float len = std::sqrt(dx * dx + dy * dy);
	if (!(len > 0.f)) return false;
	const float ux = dx / len, uy = dy / len;
	along[0] = ux; along[1] = uy;
	// A linedef's front side faces to the RIGHT of v1->v2, which is the quarter turn (uy, -ux). The
	// back side faces the other way.
	normal[0] = backSide ? -uy :  uy;
	normal[1] = backSide ?  ux : -ux;
	return true;
}

float ComputeDecalAlongOffset(float halfW, float leftOffset, bool flipX)
{
	const float fromLeft = flipX ? (halfW * 2.f - leftOffset) : leftOffset;
	return halfW - fromLeft;
}

float ComputeDecalUpOffset(float halfH, float topOffset, bool flipY)
{
	const float fromTop = flipY ? (halfH * 2.f - topOffset) : topOffset;
	return fromTop - halfH;
}

void ComputeDecalLocal(const DecalFrame &f, const float rel[3], float local[3])
{
	local[0] = Dot3(rel, f.u);
	local[1] = Dot3(rel, f.v);
	local[2] = Dot3(rel, f.n);
}

bool ComputeDecalUnwrapUV(const DecalFrame &f, const float local[3], float &outU, float &outV)
{
	// The axes arrive divided by their half-extents, so a length here converts a world distance into
	// the box units the texture coordinate is measured in.
	const float lu = Length3(f.u), lv = Length3(f.v), ln = Length3(f.n);
	if (!(lu > 0.f) || !(lv > 0.f) || !(ln > 0.f)) return false;

	const float tx = local[0], ty = local[1];
	const float r = std::sqrt(tx * tx + ty * ty);
	const float carry = std::fabs(local[2]) / ln;   // world units through the plane

	if (carry > 0.f)
	{
		// [rc4l] A mark may only wrap over a join it reaches near its own EDGE.
		//
		// This is the black slab, and it took three goes to state properly. An orthographic projection
		// simply cannot parameterise a surface that runs along its own axis: everything about that
		// surface's extent maps to no movement across the picture, so whatever coordinate is handed
		// back, some row or column of texels gets dragged along it. Where the join is out at the rim
		// that hardly shows -- the picture is nearly used up, a sliver of its edge continues past the
		// corner, and it reads exactly like a scorch creeping round. Where the join cuts through the
		// MIDDLE of the mark, the same arithmetic drags the middle of the graphic -- solid black on a
		// scorch -- across the whole face of the box, and the decal's own bounding box gets drawn as a
		// hard-edged black quad standing in the world.
		//
		// The two cases differ by exactly one number: how far the coordinate has to be pushed compared
		// with how far out it already was. Refusing to push a fragment further than its own radius
		// keeps every wrap that looks right and drops every one that cannot. It also subsumes the
		// centre of the mark, where the radius is zero and no direction exists -- nothing there can be
		// carried at all, which is the correct answer and not a special case.
		const float scale = (r > 0.f)
			? (std::fabs(tx / r) * lu + std::fabs(ty / r) * lv)
			: (lu > lv ? lu : lv);
		const float push = carry * scale;
		if (!(push < r)) { outU = -1.f; outV = -1.f; return false; }

		const float grow = (r + push) / r;
		outU = tx * grow * 0.5f + 0.5f;
		outV = ty * grow * 0.5f + 0.5f;
	}
	else
	{
		outU = tx * 0.5f + 0.5f;
		outV = ty * 0.5f + 0.5f;
	}
	return outU >= 0.f && outU <= 1.f && outV >= 0.f && outV <= 1.f;
}

}} // namespace zx::levelmesh
