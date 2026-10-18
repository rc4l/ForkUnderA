// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What moves between two tics as the renderer blends it: each interpolation's values at the start and end of the last tic, kept as plain values so a frame blends them without touching the game.

#ifndef ZX_BLEND_COMPUTE_H
#define ZX_BLEND_COMPUTE_H

#include "computation/fixedmath.h"

#include <vector>

namespace zx { namespace gamecopy {

// [rc4l] A value between its start-of-tic and end-of-tic values at smoothratio (FRACUNIT is the end), exactly as the interpolations write it.
zx::Fixed ComputeBlendFixed(zx::Fixed old, zx::Fixed cur, zx::Fixed smoothratio);

// [rc4l] A moving floor or ceiling: its plane's d and its texture height.
struct PlaneBlend { int sector; bool ceiling; zx::Fixed oldD, curD, oldTexZ, curTexZ; };
// [rc4l] A scrolling floor or ceiling texture: its x and y offsets, the y without its base.
struct ScrollBlend { int sector; bool ceiling; zx::Fixed oldX, curX, oldY, curY; };
// [rc4l] A scrolling wall texture: one part of a side.
struct WallBlend { int side; int part; zx::Fixed oldX, curX, oldY, curY; };
// [rc4l] A moving polyobject: each of its vertices by index into the level's, with x and y side by side.
struct PolyBlend { std::vector<int> vertex; std::vector<zx::Fixed> oldXY, curXY; };

// [rc4l] Everything a frame blends, filled between tics.
struct Blends
{
	std::vector<PlaneBlend> planes;
	std::vector<ScrollBlend> scrolls;
	std::vector<WallBlend> walls;
	std::vector<PolyBlend> polys;
	void Clear();
	bool Empty() const;
};

}} // namespace zx::gamecopy

#endif // ZX_BLEND_COMPUTE_H
