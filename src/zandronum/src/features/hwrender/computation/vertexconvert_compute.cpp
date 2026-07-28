// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/hwrender/computation/vertexconvert_compute.h"

namespace zx
{

double FixedRawToUnits(int64_t rawFixed)
{
	// [rc4l] int64 -> double BEFORE the scale. Doing the widening here is the whole point: a cast to
	// int/float first would drop the high 32 bits on large maps.
	return static_cast<double>(rawFixed) * kFixedToUnits;
}

float FixedRawToUnitsF(int64_t rawFixed)
{
	return static_cast<float>(FixedRawToUnits(rawFixed));
}

RenderVertexF FixedVertexToRender(int64_t doomXRaw, int64_t doomYRaw, int64_t doomZRaw)
{
	// [rc4l] Axis swap (x, y, z)_doom -> (x, z, y)_gl, each coordinate converted through the 64-bit
	// safe path.
	RenderVertexF v;
	v.x = FixedRawToUnitsF(doomXRaw);
	v.y = FixedRawToUnitsF(doomZRaw);
	v.z = FixedRawToUnitsF(doomYRaw);
	return v;
}

} // namespace zx
