// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/computation/flatfill_compute.h"

namespace zx { namespace draw2d {

namespace {

// [rc4l] Division rounding toward minus infinity, so a rectangle left of the grid's origin lands in the tile before it.
int FloorDiv(int a, int b)
{
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

} // namespace

void ComputeFlatTiles(int left, int top, int right, int bottom, int texW, int texH, bool localOrigin, std::vector<FlatTile> &out)
{
	out.clear();
	if (right <= left || bottom <= top || texW <= 0 || texH <= 0) return;
	const int ox = localOrigin ? left : 0, oy = localOrigin ? top : 0;
	for (int y = top; y < bottom;)
	{
		const int row = FloorDiv(y - oy, texH);
		const int rowTop = oy + row * texH;
		const int yEnd = rowTop + texH < bottom ? rowTop + texH : bottom;
		for (int x = left; x < right;)
		{
			const int col = FloorDiv(x - ox, texW);
			const int colLeft = ox + col * texW;
			const int xEnd = colLeft + texW < right ? colLeft + texW : right;
			FlatTile t;
			t.x = (float)x; t.y = (float)y; t.w = (float)(xEnd - x); t.h = (float)(yEnd - y);
			t.u1 = (float)(x - colLeft) / texW; t.u2 = (float)(xEnd - colLeft) / texW;
			t.v1 = (float)(y - rowTop) / texH; t.v2 = (float)(yEnd - rowTop) / texH;
			out.push_back(t);
			x = xEnd;
		}
		y = yEnd;
	}
}

}} // namespace zx::draw2d
