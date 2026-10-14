// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The frame buffer's 2D draws as the backend records them, worked out by features/draw2d: what a launch without GL draws its 2D with, and what records GL's 2D for the backend beside it.

#ifndef ZX_RECORD2D_H
#define ZX_RECORD2D_H

#include "v_video.h"
#include "features/draw2d/draw2d.h"

#include <vector>

namespace zx { namespace hwrender {

// [rc4l] One frame buffer's 2D into the backend's list, its scratch kept so a busy screen settles at its high-water mark.
class Record2DCanvas
{
public:
	void Texture(FTexture *img, const DCanvas::DrawParms &parms);
	void Line(int x1, int y1, int x2, int y2, int palcolor, uint32 color);
	void Pixel(int x, int y, int palcolor, uint32 color);
	void Clear(int left, int top, int right, int bottom, int palcolor, uint32 color);
	void Dim(PalEntry color, float damount, int x1, int y1, int w, int h);
	void FlatFill(int left, int top, int right, int bottom, FTexture *src, bool local_origin);
	void Poly(FTexture *tex, FVector2 *points, int npoints, double originx, double originy, double scalex, double scaley,
		angle_t rotation, FDynamicColormap *colormap, int lightlevel);

private:
	std::vector<Quad2D> mFlatQuads;
	zx::draw2d::PolyDraw mPoly;
	std::vector<Quad2D> mPolyTris;
};

}} // namespace zx::hwrender

#endif // ZX_RECORD2D_H
