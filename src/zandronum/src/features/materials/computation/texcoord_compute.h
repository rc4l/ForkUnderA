// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How a wall's texture offsets and width scale on a material (GL's FTexCoordInfo), which both renderers map walls by: moved here from gl/textures/gl_material.h, which includes it.

#ifndef ZX_TEXCOORD_COMPUTE_H
#define ZX_TEXCOORD_COMPUTE_H

#include "m_fixed.h"

// [rc4l] A sidedef offset in texels once the texture's own scale (`scale`) and the sidedef's (`tempScale`) are applied; world panning keeps the offset in map units unless the sidedef scales it.
fixed_t ComputeTexCoordOffset(fixed_t offset, fixed_t scale, fixed_t tempScale, bool worldPanning);

// [rc4l] The width texture offsets wrap at: the texture's own, or with world panning the width it is drawn at.
fixed_t ComputeTexAdjustWidth(int renderWidth, int width, fixed_t tempScale, bool worldPanning);

struct FTexCoordInfo
{
	int mRenderWidth;
	int mRenderHeight;
	int mWidth;
	fixed_t mScaleX;
	fixed_t mScaleY;
	fixed_t mTempScaleX;
	fixed_t mTempScaleY;
	bool mWorldPanning;

	float FloatToTexU(float v) const { return v / mRenderWidth; }
	float FloatToTexV(float v) const { return v / mRenderHeight; }
	fixed_t RowOffset(fixed_t ofs) const { return ComputeTexCoordOffset(ofs, mScaleY, mTempScaleY, mWorldPanning); }
	fixed_t TextureOffset(fixed_t ofs) const { return ComputeTexCoordOffset(ofs, mScaleX, mTempScaleX, mWorldPanning); }
	fixed_t TextureAdjustWidth() const { return ComputeTexAdjustWidth(mRenderWidth, mWidth, mTempScaleX, mWorldPanning); }
};

#endif // ZX_TEXCOORD_COMPUTE_H
