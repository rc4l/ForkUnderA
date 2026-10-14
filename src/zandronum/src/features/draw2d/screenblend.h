// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The screen's tint, worked out without GL: a sector's colormap or 3D floor fog, pickups, pain and powerups, as FGLRenderer::DrawBlend fills them.

#ifndef ZX_SCREENBLEND_H
#define ZX_SCREENBLEND_H

#include "features/hwrender/hud2d.h"   // [rc4l] Quad2D

struct sector_t;

namespace zx { namespace draw2d {

// [rc4l] One fill over the whole screen: in this colour and alpha, multiplying what is there or blended over it.
struct ScreenFill
{
	float r, g, b, a;
	bool  multiply;
};

// [rc4l] GL's area_t, where the eye is against a height-transfer sector.
enum { kViewAreaNormal = 0, kViewAreaBelow = 1, kViewAreaAbove = 2 };

// [rc4l] The fills for a view in `viewsector`, in draw order, at most two: a colormap's multiplication, then the blended tint; `fixedColormap` (invulnerability's) leaves out the sector's.
int ScreenBlends(sector_t *viewsector, int viewArea, bool fixedColormap, int paletteBrightness, ScreenFill out[2]);

// [rc4l] A fill as the 2D capture's untextured quad over the whole screen, multiplying (blend 2) or blended over (blend 0).
zx::hwrender::Quad2D PrepareScreenFill(const ScreenFill &f);

}} // namespace zx::draw2d

#endif // ZX_SCREENBLEND_H
