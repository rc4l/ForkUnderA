// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/lightband_compute.h"

namespace zx { namespace levelmesh {

float ComputeBandPlaneZ(const BandPlane &p, float x, float y)
{
	return p.ic * (-p.d - p.a * x - p.b * y);
}

int ComputeLightBandIndex(const BandPlane *planes, int count, float x, float y, float up)
{
	if (planes == 0 || count <= 0) return 0;

	// [rc4l] Walk down until the point sits above the next boundary. Mirrors SplitWall's descent:
	// it compares against lightlist[i+1]'s plane to decide where the current band ends, so the
	// boundary belongs to the band below it -- a pixel exactly on plane[i+1] is band i+1.
	for (int i = 0; i + 1 < count; i++)
	{
		const float bottom = ComputeBandPlaneZ(planes[i + 1], x, y);
		if (up > bottom) return i;
	}
	return count - 1;
}

int ComputeUploadableBandCount(int listSize)
{
	if (listSize <= 0) return 0;
	return (listSize > kMaxLightBands) ? kMaxLightBands : listSize;
}

bool ComputeNeedsGeometrySplit(int listSize)
{
	return listSize > kMaxLightBands;
}

}} // namespace zx::levelmesh
