// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/hwrender/computation/imageresize_compute.h"

#include <vector>

namespace zx
{

namespace
{

struct BoxPrecalc
{
	int boxStart;
	int boxEnd;
};

int ClampInt(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

// [rc4l] Same box windows as upstream (94b06900c): for each destination index, the inclusive
// source range whose pixels are averaged.
void ResampleBoxPrecalc(std::vector<BoxPrecalc> &boxes, int oldDim)
{
	const int newDim = (int)boxes.size();
	const double scale1 = double(oldDim) / newDim;
	const int scale2 = (int)(scale1 / 2);

	for (int dst = 0; dst < newDim; ++dst)
	{
		const int srcP = int(dst * scale1);
		BoxPrecalc &pre = boxes[dst];
		pre.boxStart = ClampInt(int(srcP - scale1 / 2.0 + 1), 0, oldDim - 1);
		const int end = srcP + scale2 > pre.boxStart + 1 ? srcP + scale2 : pre.boxStart + 1;
		pre.boxEnd = ClampInt(end, 0, oldDim - 1);
	}
}

} // namespace

void ResizeImageBoxAverage(int srcWidth, int srcHeight, const unsigned char *src,
	int dstWidth, int dstHeight, unsigned char *dst)
{
	if (srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0) return;

	std::vector<BoxPrecalc> vPre((size_t)dstHeight);
	std::vector<BoxPrecalc> hPre((size_t)dstWidth);
	ResampleBoxPrecalc(vPre, srcHeight);
	ResampleBoxPrecalc(hPre, srcWidth);

	for (int y = 0; y < dstHeight; y++)
	{
		const BoxPrecalc &v = vPre[y];
		for (int x = 0; x < dstWidth; x++)
		{
			const BoxPrecalc &h = hPre[x];

			int colored = 0;
			int total = 0;
			double sumR = 0.0, sumG = 0.0, sumB = 0.0, sumA = 0.0;

			for (int j = v.boxStart; j <= v.boxEnd; ++j)
			{
				for (int i = h.boxStart; i <= h.boxEnd; ++i)
				{
					const unsigned char *p = src + ((size_t)j * srcWidth + i) * 4;
					const int a = p[3];
					// Fully transparent pixels contribute no colour, or they would darken edges.
					if (a > 0)
					{
						sumR += p[0];
						sumG += p[1];
						sumB += p[2];
						sumA += a;
						colored++;
					}
					total++;
				}
			}

			// [rc4l] Upstream divided by 'colored' unconditionally -- a box of fully transparent
			// pixels was a division by zero. Such a box is transparent black.
			if (colored > 0)
			{
				dst[0] = (unsigned char)(sumR / colored + 0.5);
				dst[1] = (unsigned char)(sumG / colored + 0.5);
				dst[2] = (unsigned char)(sumB / colored + 0.5);
				dst[3] = (unsigned char)(sumA / total + 0.5);
			}
			else
			{
				dst[0] = dst[1] = dst[2] = dst[3] = 0;
			}
			dst += 4;
		}
	}
}

} // namespace zx
