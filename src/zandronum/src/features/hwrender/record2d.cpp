// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/record2d.h"
#include "features/hwrender/hud2d.h"   // [rc4l] Record2D

namespace zx { namespace hwrender {

void Record2DCanvas::Texture(FTexture *img, const DCanvas::DrawParms &parms)
{
	zx::draw2d::TextureDraw d;
	zx::draw2d::PrepareTexture(img, parms, d);
	Record2D(d.quad);
}

void Record2DCanvas::Line(int x1, int y1, int x2, int y2, int palcolor, uint32 color)
{
	Record2D(zx::draw2d::PrepareLine(x1, y1, x2, y2, zx::draw2d::DrawColor(palcolor, color)));
}

void Record2DCanvas::Pixel(int x, int y, int palcolor, uint32 color)
{
	Record2D(zx::draw2d::PreparePixel(x, y, zx::draw2d::DrawColor(palcolor, color)));
}

void Record2DCanvas::Clear(int left, int top, int right, int bottom, int palcolor, uint32 color)
{
	Record2D(zx::draw2d::PrepareClear(left, top, right, bottom, zx::draw2d::DrawColor(palcolor, color)));
}

void Record2DCanvas::Dim(PalEntry color, float damount, int x1, int y1, int w, int h)
{
	Record2D(zx::draw2d::PrepareDim(color, damount, x1, y1, w, h));
}

void Record2DCanvas::FlatFill(int left, int top, int right, int bottom, FTexture *src, bool local_origin)
{
	zx::draw2d::PrepareFlatFill(left, top, right, bottom, src, local_origin, mFlatQuads);
	for (size_t i = 0; i < mFlatQuads.size(); i++) Record2D(mFlatQuads[i]);
}

void Record2DCanvas::Poly(FTexture *tex, FVector2 *points, int npoints, double originx, double originy, double scalex, double scaley,
	angle_t rotation, FDynamicColormap *colormap, int lightlevel)
{
	if (!zx::draw2d::PreparePoly(tex, points, npoints, originx, originy, scalex, scaley, rotation, colormap, lightlevel, mPoly, mPolyTris)) return;
	for (size_t i = 0; i < mPolyTris.size(); i++) Record2D(mPolyTris[i]);
}

}} // namespace zx::hwrender
