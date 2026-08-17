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

void ComputeDecalCreepUV(const DecalFrame &f, const float rel[3], const float nrm[3],
                         float &outU, float &outV, float &outPath)
{
	outU = outV = 0.5f;
	outPath = 0.f;
	const float lu = Length3(f.u), lv = Length3(f.v), ln = Length3(f.n);
	if (!(lu > 0.f) || !(lv > 0.f) || !(ln > 0.f)) return;
	if (!(Length3(nrm) > 0.f)) return;

	float U[3], V[3], N[3];
	for (int i = 0; i < 3; i++) { U[i] = f.u[i] / lu; V[i] = f.v[i] / lv; N[i] = f.n[i] / ln; }

	const float sd = Dot3(rel, nrm);
	const float perp = (sd < 0.f) ? -sd : sd;

	float edge[3];
	Cross3(N, nrm, edge);
	const float edgeLen = Length3(edge);

	float t[2];
	if (edgeLen > 0.05f)
	{
		for (int i = 0; i < 3; i++) edge[i] /= edgeLen;
		float outward[3];
		Cross3(nrm, edge, outward);
		// Oriented away from the hit plane, so the sign says which side of the corner this is on.
		if (Dot3(outward, N) < 0.f)
			for (int i = 0; i < 3; i++) outward[i] = -outward[i];

		const float side = Dot3(rel, outward);
		const float across = (side < 0.f) ? -side : side;
		const float along = Dot3(rel, edge);
		const float crossed = perp + across;

		// Which way the picture continues, taken from the hit surface's own coordinate at the corner.
		float dir[3];
		const float sgn = (sd < 0.f) ? -1.f : 1.f;
		for (int i = 0; i < 3; i++) dir[i] = nrm[i] * sgn;

		const float au = Dot3(edge, U), av = Dot3(edge, V);
		const float aau = (au < 0.f) ? -au : au;
		const float aav = (av < 0.f) ? -av : av;
		if (aau >= aav)
		{
			t[0] = along * ((au < 0.f) ? -1.f : 1.f);
			t[1] = crossed * ((Dot3(dir, V) < 0.f) ? -1.f : 1.f);
		}
		else
		{
			t[0] = crossed * ((Dot3(dir, U) < 0.f) ? -1.f : 1.f);
			t[1] = along * ((av < 0.f) ? -1.f : 1.f);
		}
		// Round the BACK of the corner the only route is past its end, which costs the along-distance.
		outPath = (side >= 0.f)
			? std::sqrt(along * along + crossed * crossed)
			: (((along < 0.f) ? -along : along) + crossed);
	}
	else
	{
		// No corner: this surface is the one that was hit, or parallel to it. The picture lies flat.
		float su[3];
		const float dU = Dot3(nrm, U);
		for (int i = 0; i < 3; i++) su[i] = U[i] - nrm[i] * dU;
		if (Dot3(su, su) < 0.05f)
		{
			const float dV = Dot3(nrm, V);
			for (int i = 0; i < 3; i++) su[i] = V[i] - nrm[i] * dV;
		}
		const float suLen = Length3(su);
		if (!(suLen > 0.f)) return;
		for (int i = 0; i < 3; i++) su[i] /= suLen;

		float sv[3];
		Cross3(nrm, su, sv);
		if (Dot3(sv, V) - Dot3(sv, N) < 0.f)
			for (int i = 0; i < 3; i++) sv[i] = -sv[i];

		t[0] = Dot3(rel, su);
		t[1] = Dot3(rel, sv);
		outPath = std::sqrt(t[0] * t[0] + t[1] * t[1]) + perp;
	}

	outU = t[0] * lu * 0.5f + 0.5f;
	outV = t[1] * lv * 0.5f + 0.5f;
}

float ComputeDecalCreepReach(float path, float radius)
{
	if (!(radius > 0.f)) return 0.f;
	// smoothstep(0.5, 1.0, path / radius), inverted -- the same curve the shader uses.
	const float x = path / radius;
	if (x <= 0.5f) return 1.f;
	if (x >= 1.0f) return 0.f;
	const float u = (x - 0.5f) / 0.5f;
	return 1.f - u * u * (3.f - 2.f * u);
}

float ComputeDecalFade(int spawnTic, int decayStart, int decayTime, int now)
{
	if (decayTime <= 0) return 1.f;
	const int age = now - spawnTic;
	if (age < decayStart) return 1.f;
	const int into = age - decayStart;
	if (into >= decayTime) return 0.f;
	return 1.f - (float)into / (float)decayTime;
}

int ComputeDecalShadeLight(bool fullbright, int sectorLight)
{
	return fullbright ? 255 : sectorLight;
}

}} // namespace zx::levelmesh
