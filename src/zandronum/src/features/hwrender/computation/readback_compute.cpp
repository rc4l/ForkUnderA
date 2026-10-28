// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/readback_compute.h"

#include <cmath>

namespace zx {

void ComputeRowsToRGB(const unsigned char *src, size_t stride, int w, int h, bool bgra, unsigned char *dst)
{
	const int ri = bgra ? 2 : 0, bi = bgra ? 0 : 2;
	for (int y = 0; y < h; y++)
	{
		const unsigned char *row = src + (size_t)y * stride;
		unsigned char *out = dst + (size_t)y * w * 3;
		for (int x = 0; x < w; x++)
		{
			out[x * 3 + 0] = row[x * 4 + ri];
			out[x * 3 + 1] = row[x * 4 + 1];
			out[x * 3 + 2] = row[x * 4 + bi];
		}
	}
}

float ComputeHalfToFloat(unsigned short half)
{
	const int exponent = (half >> 10) & 31, mantissa = half & 1023;
	float v;
	if (exponent == 0) v = std::ldexp((float)mantissa, -24);
	else if (exponent == 31) v = mantissa != 0 ? NAN : INFINITY;
	else v = std::ldexp((float)(mantissa + 1024), exponent - 25);
	return (half & 0x8000) ? -v : v;
}

void ComputeHalfRowsToRGB(const unsigned char *src, size_t stride, int w, int h, unsigned char *dst)
{
	for (int y = 0; y < h; y++)
	{
		const unsigned char *row = src + (size_t)y * stride;
		unsigned char *out = dst + (size_t)y * w * 3;
		for (int x = 0; x < w * 3; x++)
		{
			const unsigned char *c = row + (x / 3) * 8 + (x % 3) * 2;
			const float v = ComputeHalfToFloat((unsigned short)(c[0] | (c[1] << 8)));
			out[x] = std::isnan(v) ? 0 : (unsigned char)std::lround(std::fmin(std::fmax(v, 0.f), 1.f) * 255.f);
		}
	}
}

} // namespace zx
