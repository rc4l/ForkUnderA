// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/lightside_compute.h"

#include <math.h>

namespace zx { namespace hwrender {

const float kLightOnPlaneSlack = 0.1f;

float ComputePlaneZAt(const SecPlaneF &p, float x, float y)
{
	if (p.c == 0.f) return 0.f;   // vertical: not a flat, and it has no height here
	return (-p.d - p.a * x - p.b * y) / p.c;
}

bool ComputeGLLightReaches(const SecPlaneF &p, float lx, float ly, float lz, bool ceiling)
{
	// gl_flats.cpp: skip when (planeh < light->z && ceiling) || (planeh > light->z && !ceiling).
	// Both comparisons are strict, so a light lying exactly in the plane is KEPT -- which is not an
	// edge case in Doom but the common one, since anything that dies on a surface rests on it.
	const float planeh = ComputePlaneZAt(p, lx, ly);
	if (ceiling) return !(planeh < lz);
	return !(planeh > lz);
}

void ComputeMeshPlane(const SecPlaneF &p, bool viewedFromBelow, float outNormal[3], float *outPlaneD)
{
	// The mesh is (x, z-up, y): a secplane's (a, b, c) becomes (a, c, b) here.
	const float len = sqrtf(p.a * p.a + p.b * p.b + p.c * p.c);
	if (len <= 0.0001f)
	{
		outNormal[0] = outNormal[1] = outNormal[2] = 0.f;
		if (outPlaneD) *outPlaneD = 0.f;
		return;
	}

	// [rc4l] Turned to face the side the surface is SEEN from.
	//
	// A floor's plane points DOWN in ZDoom's convention (c is negative), so a floor seen from above
	// has to be flipped to point up. Get this backwards and the side test finds every light in the
	// room behind the surface and lights none of them, which looks exactly like a light that is out
	// of range.
	const float up = p.c / len;
	const bool flipped = (up > 0.f) ? viewedFromBelow : (up < 0.f ? !viewedFromBelow : false);
	const float sign = flipped ? -1.f : 1.f;

	outNormal[0] = sign * p.a / len;
	outNormal[1] = sign * p.c / len;
	outNormal[2] = sign * p.b / len;

	// The vertex shader computes dot(normal, position) for a vertex ON the plane, and every such
	// point gives the same number: a*x + b*y + c*z = -d, so this is -sign*d/len without needing a
	// vertex to evaluate it at.
	if (outPlaneD) *outPlaneD = -sign * p.d / len;
}

bool ComputeShaderLightReaches(const float normal[3], float planeD, const float lightMesh[3])
{
	if (normal[0] == 0.f && normal[1] == 0.f && normal[2] == 0.f)
		return false;   // no side: a billboard, whose light the CPU has already folded in
	const float side = normal[0] * lightMesh[0] + normal[1] * lightMesh[1] + normal[2] * lightMesh[2]
	                 - planeD;
	return side >= -kLightOnPlaneSlack;
}

} }   // namespace zx::hwrender
