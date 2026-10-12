// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] FGLRenderer::DrawBlend's choices: which 3D floor band tints the screen, and how a colormap's tint is brightened.

#ifndef ZX_SCREENBLEND_COMPUTE_H
#define ZX_SCREENBLEND_COMPUTE_H

namespace zx { namespace draw2d {

// [rc4l] A 3D floor light band as DrawBlend reads it: the height it ends at below, raw fixed point, its blend as ARGB, and whether an FF_FADEWALLS floor casts it.
struct BlendBand
{
	long long bottom;
	unsigned  blend;
	bool      fadeWalls;
};

// [rc4l] The blend of the first band ending below the eye that no FADEWALLS floor casts: none when it is the sector's own fade, half strength for a Legacy blend with no alpha, and none when no band qualifies.
unsigned ComputeBandBlend(const BlendBand *bands, int count, long long viewz, unsigned sectorFade);

// [rc4l] A colormap's average as R_BlendForColormap gives it (alpha 255), too dark alone, brightened to the palette's brightness; any other blend unchanged.
unsigned ComputeBrightenedBlend(unsigned argb, int paletteBrightness);

}} // namespace zx::draw2d

#endif // ZX_SCREENBLEND_COMPUTE_H
