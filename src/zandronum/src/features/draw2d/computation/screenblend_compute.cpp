// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/computation/screenblend_compute.h"

namespace zx { namespace draw2d {

unsigned ComputeBandBlend(const BlendBand *bands, int count, long long viewz, unsigned sectorFade)
{
	int i = 0;
	while (i < count && (bands[i].bottom >= viewz || bands[i].fadeWalls)) i++;
	if (i == count) return 0;
	// [rc4l] 3D floor 'fog' is a blend, but not when it is only the sector's own fade.
	unsigned blend = bands[i].blend;
	if (blend == sectorFade) blend = 0;
	// [rc4l] Legacy maps give a blend no alpha.
	if ((blend >> 24) == 0 && blend != 0) blend |= 128u << 24;
	return blend;
}

unsigned ComputeBrightenedBlend(unsigned argb, int paletteBrightness)
{
	if ((argb >> 24) != 255) return argb;
	int r = (argb >> 16) & 255, g = (argb >> 8) & 255, b = argb & 255;
	int maxcol = paletteBrightness;
	if (r > maxcol) maxcol = r;
	if (g > maxcol) maxcol = g;
	if (b > maxcol) maxcol = b;
	if (maxcol == 0) return argb;
	r = r * 255 / maxcol;
	g = g * 255 / maxcol;
	b = b * 255 / maxcol;
	return (255u << 24) | (unsigned(r) << 16) | (unsigned(g) << 8) | unsigned(b);
}

}} // namespace zx::draw2d
