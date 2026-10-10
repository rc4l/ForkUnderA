// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/flood_compute.h"

namespace zx { namespace hwrender {

bool ComputeFloodPoint(float eyeX, float eyeY, float eyeZ, float fragX, float fragY, float fragZ, float planeZ,
	float &x, float &y)
{
	const float rise = fragZ - eyeZ;
	if (rise == 0.f) return false;
	const float t = (planeZ - eyeZ) / rise;
	if (!(t > 0.f && t <= 1.f)) return false;
	x = eyeX + t * (fragX - eyeX);
	y = eyeY + t * (fragY - eyeY);
	return true;
}

}} // namespace zx::hwrender
