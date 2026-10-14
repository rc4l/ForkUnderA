// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/stereo_compute.h"

#include <math.h>

namespace zx { namespace hwrender {

StereoMode ComputeStereoMode(int vrMode)
{
	StereoMode s = { 1, { 0.f, 0.f }, { { 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f } } };
	switch (vrMode)
	{
	case 1:   // [rc4l] GreenMagenta: green to the left eye, red and blue to the right
	case 2:   // [rc4l] RedCyan: red to the left eye, green and blue to the right
	{
		s.eyes = 2;
		s.shift[0] = -0.5f; s.shift[1] = 0.5f;
		const int keep = vrMode == 1 ? 1 : 0;
		for (int c = 0; c < 3; c++) { s.mask[0][c] = c == keep ? 1.f : 0.f; s.mask[1][c] = c == keep ? 0.f : 1.f; }
		break;
	}
	case 5: s.shift[0] = -0.5f; break;
	case 6: s.shift[0] = 0.5f; break;
	}
	return s;
}

void ComputeEyeOffset(float shiftMeters, float yawDeg, float hunitsPerMeter, float &dx, float &dy)
{
	const float yaw = yawDeg * 3.14159265f / 180.f;
	// [rc4l] ScopedViewShifter takes x away and adds y.
	dx = -cosf(yaw) * hunitsPerMeter * shiftMeters;
	dy = sinf(yaw) * hunitsPerMeter * shiftMeters;
}

float ComputeEyeNdcShift(float shiftMeters, float screenDist, float fovY, float aspect)
{
	const float halfWidth = tanf(fovY * 0.5f) * aspect;
	if (screenDist <= 0.f || halfWidth <= 0.f) return 0.f;
	return shiftMeters / (screenDist * halfWidth);
}

void ComputeShiftNdcX(float ndc, float m[16])
{
	for (int col = 0; col < 4; col++) m[col * 4 + 0] += ndc * m[col * 4 + 3];
}

}} // namespace zx::hwrender
