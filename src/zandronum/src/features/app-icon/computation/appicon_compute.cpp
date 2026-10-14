// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/app-icon/computation/appicon_compute.h"

namespace zx { namespace appicon {

bool ComputeIconDownscale(const unsigned char *src, int w, int h, int size, unsigned char *dst)
{
	if (size <= 0 || w < size || h < size) return false;
	for (int y = 0; y < size; y++)
	{
		const int y0 = y * h / size, y1 = (y + 1) * h / size;
		for (int x = 0; x < size; x++)
		{
			const int x0 = x * w / size, x1 = (x + 1) * w / size;
			unsigned long long c[3] = { 0, 0, 0 }, alpha = 0, count = 0;
			for (int sy = y0; sy < y1; sy++)
				for (int sx = x0; sx < x1; sx++)
				{
					const unsigned char *t = src + (sy * w + sx) * 4;
					for (int k = 0; k < 3; k++) c[k] += (unsigned long long)t[k] * t[3];
					alpha += t[3];
					count++;
				}
			unsigned char *o = dst + (y * size + x) * 4;
			for (int k = 0; k < 3; k++) o[k] = (unsigned char)(alpha ? c[k] / alpha : 0);
			o[3] = (unsigned char)(alpha / count);
		}
	}
	return true;
}

}} // namespace zx::appicon
