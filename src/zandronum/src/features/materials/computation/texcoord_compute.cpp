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

// [rc4l] One axis: the drawn size and scale as they are, or at the sidedef's scale the size rounded from twice its texel count over the scale.
static void ScaleAxis(int size, int renderSize, fixed_t texScale, fixed_t sideScale, int &outRender, fixed_t &outScale, fixed_t &outTemp)
{
	if (sideScale == FRACUNIT)
	{
		outRender = renderSize;
		outScale = texScale;
		outTemp = FRACUNIT;
		return;
	}
	const fixed_t scale = FixedMul(sideScale, texScale);
	const int twice = (int)((size << 17) / scale);
	outRender = (twice >> 1) + (twice & 1);
	outScale = scale;
	outTemp = sideScale;
}

void ComputeTexCoordInfo(const TexCoordSource &src, fixed_t x, fixed_t y, FTexCoordInfo *tci)
{
	ScaleAxis(src.width, src.renderWidth, src.xScale, x, tci->mRenderWidth, tci->mScaleX, tci->mTempScaleX);
	ScaleAxis(src.height, src.renderHeight, src.yScale, y, tci->mRenderHeight, tci->mScaleY, tci->mTempScaleY);
	if (src.canvas)
	{
		tci->mScaleY = -tci->mScaleY;
		tci->mRenderHeight = -tci->mRenderHeight;
	}
	tci->mWorldPanning = src.worldPanning;
	tci->mWidth = src.width;
}

fixed_t ComputeTexAdjustWidth(int renderWidth, int width, fixed_t tempScale, bool worldPanning)
{
	if (!worldPanning) return fixed_t::FromRaw(width);
	if (tempScale == FRACUNIT) return fixed_t::FromRaw(renderWidth);
	return FixedDiv(fixed_t::FromRaw(width), tempScale);
}
