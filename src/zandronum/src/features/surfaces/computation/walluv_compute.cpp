// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/computation/walluv_compute.h"

namespace zx { namespace surfaces {

float ComputeWallU(float alongLine, float xOffset, float texWidth)
{
	if (texWidth <= 0.f) return 0.f;
	return (alongLine + xOffset) / texWidth;
}

float ComputeWallV(float z, float textureTop, float texHeight)
{
	if (texHeight <= 0.f) return 0.f;
	// Downward: v grows as z falls, because Doom hangs a wall texture from its top edge.
	return (textureTop - z) / texHeight;
}

float ComputeTextureTop(float partTop, float partBottom, float texHeight, bool pegBottom,
	float rowOffset)
{
	// [rc4l] Pegged to the bottom: the texture's LAST row lands on the bottom of the part, so its top
	// is one texture-height above that -- which is what keeps a door's picture still while the door
	// slides, and what lines a step's texture up with the floor instead of the ceiling.
	if (pegBottom) return partBottom + texHeight + rowOffset;
	return partTop + rowOffset;
}

}} // namespace zx::surfaces
