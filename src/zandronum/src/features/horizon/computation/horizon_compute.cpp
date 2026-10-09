// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/horizon/computation/horizon_compute.h"

#include <math.h>

namespace zx { namespace horizon {

bool ComputeIsHorizonLine(int lineSpecial)
{
	return lineSpecial == kLineHorizonSpecial;
}

HorizonHalves ComputeHorizonHalves(bool floorSky, bool ceilingSky)
{
	HorizonHalves h;
	h.floorPlane = !floorSky;
	h.ceilingPlane = !ceilingSky;
	h.skyAbove = ceilingSky;
	return h;
}

HorizonSpan ComputeHorizonWindowSpan(float floorZ, float ceilingZ, bool floorSky, bool ceilingSky)
{
	HorizonSpan s;
	s.bottom = floorSky ? -kHorizonSkyReach : floorZ;
	s.top = ceilingSky ? kHorizonSkyReach : ceilingZ;
	return s;
}

float ComputeHorizonSplit(float eyeZ, float floorZ, float ceilingZ)
{
	if (eyeZ < floorZ) return floorZ;
	if (eyeZ > ceilingZ) return ceilingZ;
	return eyeZ;
}

HorizonSpan ComputeHorizonHalfSpan(bool ceilingHalf, float floorZ, float ceilingZ, float eyeZ)
{
	const float split = ComputeHorizonSplit(eyeZ, floorZ, ceilingZ);
	HorizonSpan s;
	s.bottom = ceilingHalf ? split : floorZ;
	s.top = ceilingHalf ? ceilingZ : split;
	return s;
}

void ComputeHorizonLightInputs(bool fullbright, int lightLevel, int extraLight, int &level, int &rel)
{
	level = fullbright ? 255 : lightLevel;
	rel = fullbright ? 0 : extraLight;
}

HorizonHit ComputeHorizonRayHit(float eyeX, float eyeY, float eyeZ,
                                float throughX, float throughY, float throughZ,
                                float planeZ, float halfExtent)
{
	const float dx = throughX - eyeX, dy = throughY - eyeY, dz = throughZ - eyeZ;
	const float toPlane = planeZ - eyeZ;

	HorizonHit h;
	h.z = planeZ;
	float ox, oy;
	// [rc4l] Heading toward the plane when the rise to cover and the ray's own rise share a sign.
	if (dz * toPlane > 0.f)
	{
		const float t = toPlane / dz;
		ox = dx * t;
		oy = dy * t;
	}
	else
	{
		// [rc4l] Level with the plane or leaving it: the horizon itself, out along the heading.
		const float len = sqrtf(dx * dx + dy * dy);
		const float far = 2.f * halfExtent;
		ox = (len > 0.f) ? dx / len * far : 0.f;
		oy = (len > 0.f) ? dy / len * far : 0.f;
	}

	const float m = fabsf(ox) > fabsf(oy) ? fabsf(ox) : fabsf(oy);
	h.withinSquare = (m <= halfExtent);
	if (!h.withinSquare)
	{
		const float s = halfExtent / m;
		ox *= s;
		oy *= s;
	}
	h.x = eyeX + ox;
	h.y = eyeY + oy;
	return h;
}

bool ComputeHorizonFacesEye(float x1, float y1, float x2, float y2, float eyeX, float eyeY)
{
	return (x2 - x1) * (eyeY - y1) - (y2 - y1) * (eyeX - x1) < 0.f;
}

namespace {

double Det3(const double m[3][3])
{
	return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
	     - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
	     + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

} // namespace

bool ComputeEyeFromViewProjection(const float mvp[16], float eye[3])
{
	// [rc4l] Rows 0, 1 and 3 of M times (E, 1) are zero -- three equations, solved by Cramer's rule.
	double a[3][3], b[3];
	const int rows[3] = { 0, 1, 3 };
	for (int i = 0; i < 3; i++)
	{
		const int r = rows[i];
		a[i][0] = mvp[0 * 4 + r]; a[i][1] = mvp[1 * 4 + r]; a[i][2] = mvp[2 * 4 + r];
		b[i] = -mvp[3 * 4 + r];
	}
	const double det = Det3(a);
	if (fabs(det) < 1e-12) return false;
	for (int c = 0; c < 3; c++)
	{
		double m[3][3];
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++) m[i][j] = (j == c) ? b[i] : a[i][j];
		eye[c] = (float)(Det3(m) / det);
	}
	return true;
}

bool ComputeQuadMayBeVisible(const float mvp[16], const float corners[4][3])
{
	// [rc4l] One bit per clip plane a corner is outside: x<-w, x>w, y<-w, y>w, z<0, z>w.
	int all = 0x3f;
	for (int k = 0; k < 4; k++)
	{
		float c[4];
		for (int r = 0; r < 4; r++)
			c[r] = mvp[0 * 4 + r] * corners[k][0] + mvp[1 * 4 + r] * corners[k][1] +
			       mvp[2 * 4 + r] * corners[k][2] + mvp[3 * 4 + r];
		int out = 0;
		if (c[0] < -c[3]) out |= 1;
		if (c[0] >  c[3]) out |= 2;
		if (c[1] < -c[3]) out |= 4;
		if (c[1] >  c[3]) out |= 8;
		if (c[2] < 0.f)   out |= 16;
		if (c[2] >  c[3]) out |= 32;
		all &= out;
	}
	return all == 0;
}

}} // namespace zx::horizon
