// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/mirrorsheen_compute.h"

#include <math.h>

namespace zx { namespace hwrender {

bool ComputeNormalMatrix(const float m[16], float out[9])
{
	// [rc4l] The 3x3 by columns, as computeNormalMatrix copies it out of the 4x4.
	const float a[9] = { m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10] };
	const float det = a[0] * (a[4] * a[8] - a[5] * a[7]) + a[1] * (a[5] * a[6] - a[8] * a[3]) + a[2] * (a[3] * a[7] - a[4] * a[6]);
	if (det == 0.f) return false;
	const float inv = 1.f / det;
	out[0] = (a[4] * a[8] - a[5] * a[7]) * inv;
	out[1] = (a[5] * a[6] - a[8] * a[3]) * inv;
	out[2] = (a[3] * a[7] - a[4] * a[6]) * inv;
	out[3] = (a[2] * a[7] - a[1] * a[8]) * inv;
	out[4] = (a[0] * a[8] - a[2] * a[6]) * inv;
	out[5] = (a[1] * a[6] - a[7] * a[0]) * inv;
	out[6] = (a[1] * a[5] - a[4] * a[2]) * inv;
	out[7] = (a[2] * a[3] - a[0] * a[5]) * inv;
	out[8] = (a[0] * a[4] - a[3] * a[1]) * inv;
	return true;
}

static void Normalize(float v[3])
{
	const float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (len > 0.f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

void ComputeSphereMapUV(const float eyePos[3], const float eyeNormal[3], float &s, float &t)
{
	float u[3] = { eyePos[0], eyePos[1], eyePos[2] }, n[3] = { eyeNormal[0], eyeNormal[1], eyeNormal[2] };
	Normalize(u);
	Normalize(n);
	// [rc4l] GLSL's reflect(u, n), then the sphere map's projection of the reflected ray.
	const float d = 2.f * (n[0] * u[0] + n[1] * u[1] + n[2] * u[2]);
	const float r[3] = { u[0] - d * n[0], u[1] - d * n[1], u[2] - d * n[2] };
	const float m = 2.f * sqrtf(r[0] * r[0] + r[1] * r[1] + (r[2] + 1.f) * (r[2] + 1.f));
	s = r[0] / m + 0.5f;
	t = r[1] / m + 0.5f;
}

void ComputeMirrorSheenConstants(const float view[16], float normX, float normZ, const float rgb[3], float softLight,
	float fogMode, float fogDensity, bool drawn, int recursions, float out[kMirrorSheenFloats])
{
	for (int i = 0; i < 16; i++) out[i] = view[i];
	// [rc4l] The texture matrix RenderMirrorSurface loads is the view's normal matrix, and the normal it turns is the wall's, flat.
	float nm[9];
	float en[3] = { 0.f, 0.f, 0.f };
	if (ComputeNormalMatrix(view, nm))
		for (int r = 0; r < 3; r++) en[r] = nm[r] * normX + nm[6 + r] * normZ;
	Normalize(en);
	out[16] = en[0]; out[17] = en[1]; out[18] = en[2]; out[19] = 0.f;
	out[20] = rgb[0]; out[21] = rgb[1]; out[22] = rgb[2]; out[23] = drawn ? kMirrorSheenAlpha : 0.f;
	out[24] = softLight; out[25] = fogMode; out[26] = fogDensity; out[27] = (float)recursions;
}

}} // namespace zx::hwrender
