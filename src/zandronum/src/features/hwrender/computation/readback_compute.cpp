// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/readback_compute.h"

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

} // namespace zx
