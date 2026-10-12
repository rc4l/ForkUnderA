// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A rectangle tiled with a texture as DCanvas::FlatFill draws it, cut at the texture's edges because the backend's 2D layer clamps rather than wraps.

#ifndef ZX_FLATFILL_COMPUTE_H
#define ZX_FLATFILL_COMPUTE_H

#include <vector>

namespace zx { namespace draw2d {

// [rc4l] One copy of the texture, or the part of one inside the rectangle: where it lands in screen pixels and which part of the texture it shows, 0 to 1.
struct FlatTile
{
	float x, y, w, h;
	float u1, v1, u2, v2;
};

// [rc4l] The tiles covering left..right by top..bottom with a texW by texH texture, the grid starting at the screen's origin or, with localOrigin, at the rectangle's; out is replaced.
void ComputeFlatTiles(int left, int top, int right, int bottom, int texW, int texH, bool localOrigin, std::vector<FlatTile> &out);

}} // namespace zx::draw2d

#endif // ZX_FLATFILL_COMPUTE_H
