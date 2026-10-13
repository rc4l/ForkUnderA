// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/materials/computation/texcoord_compute.h"

fixed_t ComputeTexCoordOffset(fixed_t offset, fixed_t scale, fixed_t tempScale, bool worldPanning)
{
	if (tempScale == FRACUNIT)
	{
		if (scale == FRACUNIT || worldPanning) return offset;
		return FixedDiv(offset, scale);
	}
	return worldPanning ? FixedDiv(offset, tempScale) : FixedDiv(offset, scale);
}

fixed_t ComputeTexAdjustWidth(int renderWidth, int width, fixed_t tempScale, bool worldPanning)
{
	if (!worldPanning) return fixed_t::FromRaw(width);
	if (tempScale == FRACUNIT) return fixed_t::FromRaw(renderWidth);
	return FixedDiv(fixed_t::FromRaw(width), tempScale);
}
