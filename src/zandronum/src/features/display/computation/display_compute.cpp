// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/display/computation/display_compute.h"

#include <math.h>

namespace zx { namespace display {

namespace {

template<class T> T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

void ComputeGammaRamp(float gamma, float contrast, float brightness, unsigned short ramp[768])
{
	gamma = Clamp(gamma, 0.1f, 4.f);
	contrast = Clamp(contrast, 0.1f, 3.f);
	brightness = Clamp(brightness, -0.8f, 0.8f);
	const double invgamma = 1 / gamma;
	const double norm = pow(255., invgamma - 1);
	for (int i = 0; i < 256; i++)
	{
		double val = i * contrast - (contrast - 1) * 127;
		if (gamma != 1) val = pow(val, invgamma) / norm;
		val += brightness * 128;
		ramp[i] = ramp[i + 256] = ramp[i + 512] = (unsigned short)Clamp<double>(val * 256, 0, 0xffff);
	}
}

int ComputePaletteBrightness(const unsigned *argb, int count)
{
	int rr = 0, gg = 0, bb = 0;
	for (int i = 0; i < count; i++)
	{
		rr += (argb[i] >> 16) & 255;
		gg += (argb[i] >> 8) & 255;
		bb += argb[i] & 255;
	}
	// [rc4l] UpdatePalette divides by 256 whatever the count, as the palette always has 256 entries.
	rr >>= 8; gg >>= 8; bb >>= 8;
	return (rr * 77 + gg * 143 + bb * 35) / 255;
}

}} // namespace zx::display
