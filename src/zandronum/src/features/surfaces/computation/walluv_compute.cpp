// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/computation/walluv_compute.h"

#include <math.h>

namespace zx { namespace surfaces {

float ComputeWallU(float alongLine, float xOffset, float texWidth)
{
	if (texWidth <= 0.f) return 0.f;
	return (alongLine + xOffset) / texWidth;
}

void ComputePolyobjSegFractions(float lx1, float ly1, float lx2, float ly2,
	float sx1, float sy1, float sx2, float sy2, float &left, float &right)
{
	const float dx = lx2 - lx1, dy = ly2 - ly1;
	if (dx == 0.f && dy == 0.f) { left = 0.f; right = 1.f; return; }
	if (fabsf(dx) > fabsf(dy)) { left = (sx1 - lx1) / dx; right = (sx2 - lx1) / dx; }
	else                       { left = (sy1 - ly1) / dy; right = (sy2 - ly1) / dy; }
}

float ComputeWallV(float z, float textureTop, float texHeight)
{
	if (texHeight == 0.f) return 0.f;
	// Downward: v grows as z falls, because Doom hangs a wall texture from its top edge.
	return (textureTop - z) / texHeight;
}

float ComputeCanvasWallV(float v, bool canvas)
{
	return canvas ? v + 1.f : v;
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


bool ComputeWallClampsY(float vUpLeft, float vUpRight, float vLoLeft, float vLoRight)
{
	// Written as GL writes it: either the wall starts exactly at the top of the texture and ends
	// within it, or it ends exactly at the bottom and starts within it. Both tests are on exact
	// equality there, so they are here -- a tolerance would clamp walls GL leaves wrapping.
	return ( vUpLeft == 0.f && vUpRight == 0.f && vLoLeft <= 1.f && vLoRight <= 1.f ) ||
	       ( vUpLeft >= 0.f && vUpRight >= 0.f && vLoLeft == 1.f && vLoRight == 1.f );
}

}} // namespace zx::surfaces
