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

float ComputeTextureTop(float refCeiling, float refFloor, float texHeight, bool pegged,
	float rowOffset, float vOffset)
{
	float top = refCeiling + rowOffset;
	// [rc4l] GL's exact shift, from DoTexture: the texture is pushed down until its bottom row sits
	// on refFloor. Written as it is written there, so the two cannot drift.
	if (pegged) top += texHeight - ((refCeiling - refFloor) + vOffset);
	return top;
}

}} // namespace zx::surfaces
