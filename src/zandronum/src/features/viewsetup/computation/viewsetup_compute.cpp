// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/viewsetup/computation/viewsetup_compute.h"
#include "features/fixed64/computation/xsround_compute.h"

#include <math.h>

namespace zx { namespace viewsetup {

static const double kPi = 3.14159265358979323846;

float ComputeStretchedPitch(unsigned viewPitch, float pixelStretch)
{
	// [rc4l] bam2rad, then folded to a signed pitch and clamped to straight up or down.
	double radPitch = double(viewPitch >> 1) * (kPi / 1073741824.0);
	if (radPitch > kPi) radPitch -= 2 * kPi;
	if (radPitch < -kPi / 2) radPitch = -kPi / 2;
	else if (radPitch > kPi / 2) radPitch = kPi / 2;
	const double angx = cos(radPitch);
	const double angy = sin(radPitch) * pixelStretch;
	const double alen = sqrt(angx * angx + angy * angy);
	return (float)(asin(angy / alen) * 180.f / kPi);
}

unsigned ComputeFrustumAngle(float pitchDeg, float fovDeg, int aspectMultiplier)
{
	const float tilt = fabsf(pitchDeg);
	if (tilt > 46.0f) return 0xffffffffu;
	const double angle = 2.0 + (45.0 + (tilt / 1.9)) * fovDeg * 48.0 / aspectMultiplier / 90.0;
	const unsigned a1 = ComputeXsRoundToUInt(angle / 180. * 2147483648.0);
	return a1 >= 0x80000000u ? 0xffffffffu : a1;
}

}} // namespace zx::viewsetup
