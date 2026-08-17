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

void Cross3(const float a[3], const float b[3], float out[3])
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

} // namespace

float ComputeDecalChord(float distanceToSurface, float radius)
{
	const float k = radius * radius - distanceToSurface * distanceToSurface;
	return (k <= 0.f) ? 0.f : std::sqrt(k);
}

float ComputeDecalReach(float halfW, float halfH)
{
	return std::sqrt(halfW * halfW + halfH * halfH) * 1.5f;
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

bool ComputeDecalSurfaceUV(const DecalFrame &f, const float rel[3], const float nrm[3],
                           float chord, float radius, float &outU, float &outV)
{
	// The picture shrinks with the disc the blast cuts in this surface: whole where it landed, gone
	// where the sphere no longer reaches. Without this a receding surface took a full-size copy.
	if (!(radius > 0.f) || !(chord > 0.f)) return false;
	const float shrink = chord / radius;
	const float lu = Length3(f.u), lv = Length3(f.v), ln = Length3(f.n);
	if (!(lu > 0.f) || !(lv > 0.f) || !(ln > 0.f)) return false;
	if (!(Length3(nrm) > 0.f)) return false;

	float U[3], V[3], N[3];
	for (int i = 0; i < 3; i++) { U[i] = f.u[i] / lu; V[i] = f.v[i] / lv; N[i] = f.n[i] / ln; }

	// The mark's across-axis, laid into this surface. Where it lies along the surface's own normal
	// there is nothing left of it to lay -- on a floor, "along the wall" still means something while
	// "up the wall" does not -- so the next axis is tried in turn.
	float su[3];
	const float dU = Dot3(nrm, U);
	for (int i = 0; i < 3; i++) su[i] = U[i] - nrm[i] * dU;
	if (Dot3(su, su) < 0.05f)
	{
		const float dV = Dot3(nrm, V);
		for (int i = 0; i < 3; i++) su[i] = V[i] - nrm[i] * dV;
	}
	if (Dot3(su, su) < 0.05f)
	{
		const float dN = Dot3(nrm, N);
		for (int i = 0; i < 3; i++) su[i] = N[i] - nrm[i] * dN;
	}
	const float suLen = Length3(su);
	if (!(suLen > 0.f)) return false;
	for (int i = 0; i < 3; i++) su[i] /= suLen;

	float sv[3];
	Cross3(nrm, su, sv);
	// Which of the two perpendiculars is "up the picture". On the surface that was hit this is the
	// mark's own V; on a floor, where V lies along the normal and decides nothing, the tie is broken
	// by pointing away from the surface that was hit. Both are fixed in world space, so the answer
	// cannot depend on where anyone is standing.
	if (Dot3(sv, V) - Dot3(sv, N) < 0.f)
		for (int i = 0; i < 3; i++) sv[i] = -sv[i];

	outU = Dot3(rel, su) * lu / shrink * 0.5f + 0.5f;
	outV = Dot3(rel, sv) * lv / shrink * 0.5f + 0.5f;
	return outU >= 0.f && outU <= 1.f && outV >= 0.f && outV <= 1.f;
}

}} // namespace zx::levelmesh
