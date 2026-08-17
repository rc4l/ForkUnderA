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

// [rc4l] Which way to carry, ramped through the middle rather than switched.
//
// The direction wanted is "the way we were already going", which is sign(t) -- and sign() jumps. A
// fragment a hair either side of the decal's centre line would get +carry or -carry, tearing the
// coordinate in two down the middle of every mark on anything but a flat surface. Worse than it
// looks, because a shader picks its mip level from the DERIVATIVE of this coordinate: a jump reads
// as an enormous derivative, the smallest mip is chosen, and the mark goes pale and streaky along
// the tear.
//
// Ramping over a narrow band gives up a little accuracy exactly where the carry contributes least
// -- near the centre line, where there is barely any distance to carry -- and buys continuity.
const float kCarryRampBand = 0.12f;

float CarryDirection(float t)
{
	const float d = t / kCarryRampBand;
	return (d > 1.f) ? 1.f : ((d < -1.f) ? -1.f : d);
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

bool ComputeDecalUnwrapUV(const DecalFrame &f, const float local[3], const float nrm[3],
                          float &outU, float &outV)
{
	float tx = local[0], ty = local[1];

	// The axes arrive divided by their half-extents, so the ratio of two lengths converts a distance
	// measured in one axis's units into another's.
	const float lu = Length3(f.u), lv = Length3(f.v), ln = Length3(f.n);
	if (!(lu > 0.f) || !(lv > 0.f) || !(ln > 0.f)) return false;

	// Which axis has the surface turned about? A surface turned about V -- a wall met round a
	// vertical corner -- has a normal with a component along U, and vice versa.
	const float turnU = std::fabs(Dot3(nrm, f.u)) / lu;
	const float turnV = std::fabs(Dot3(nrm, f.v)) / lv;
	const float carry = std::fabs(local[2]) / ln;

	// [rc4l] Split the carry between the axes rather than choosing one.
	//
	// Choosing was a hard switch on a normal the shader recovers from depth derivatives, and at a
	// grazing angle those are differences of nearly-equal large numbers. The normal is therefore
	// noisy, the switch flipped pixel to pixel and frame to frame, and the mark visibly reshaped as
	// the camera moved. Weighting gives the same answer wherever the answer is clear -- a floor is
	// all V, a side wall all U -- and a smooth mixture where it is not.
	const float sum = turnU + turnV;
	const float wu = (sum > 1e-5f) ? turnU / sum : 0.f;
	const float wv = (sum > 1e-5f) ? turnV / sum : 0.f;

	// Keep going the way we were already going: a floor below the mark continues downwards, a ceiling
	// above it upwards, a wall to the right rightwards.
	tx += CarryDirection(local[0]) * carry * lu * wu;
	ty += CarryDirection(local[1]) * carry * lv * wv;

	outU = tx * 0.5f + 0.5f;
	outV = ty * 0.5f + 0.5f;
	return outU >= 0.f && outU <= 1.f && outV >= 0.f && outV <= 1.f;
}

}} // namespace zx::levelmesh
