// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where the picture sits on a wall, worked out from the map.
//
// wallgeom_compute says how tall a sidedef part is. This says what is printed on it: the horizontal
// coordinate from how far along the line the wall starts, the vertical from where the texture is
// pegged. Both are ratios of the texture's own size, which is what makes them checkable without a
// renderer -- a u of 2.0 means "twice across the texture", whatever the texture turns out to be.
//
// The two halves are separate functions on purpose. A wall drawn at the wrong height and a wall
// drawn with the picture in the wrong place look identical in a screenshot -- both are "the texture
// is wrong there" -- and they have completely different causes. Keeping them apart means a
// disagreement can name which one it is.
//
// Engine-free. Distances in map units, sizes in map units, offsets in map units.

#ifndef ZX_WALLUV_COMPUTE_H
#define ZX_WALLUV_COMPUTE_H

namespace zx { namespace surfaces {

// The horizontal texture coordinate at a point a given distance along the line.
//
// `alongLine` is measured from the start of the LINEDEF, not the seg: a line split into several segs
// shares one continuous texture across them, and measuring from each seg's own start is how a wall
// ends up with the pattern restarting at every vertex.
//
// Not wrapped into 0..1. The renderer wants the running value so the texture repeats across a long
// wall, and clamping it here would tile a corridor with one stretched copy.
float ComputeWallU(float alongLine, float xOffset, float texWidth);

// The vertical texture coordinate at a height.
//
// Measured DOWN from the pegging reference, because Doom hangs wall textures from a top edge: v = 0
// at textureTop and v = 1 one texture-height below it. A wall whose bottom is above the reference
// gets a negative v, which is correct and normal -- an upper texture pegged to the ceiling of the
// sector behind it does exactly that.
float ComputeWallV(float z, float textureTop, float texHeight);

// [rc4l] Where the texture is pegged from, which is the whole of Doom's texture alignment.
//
// Unpegged (the default) hangs the texture from the top of the part being drawn. Pegged -- the
// DONTPEG flags -- anchors it to the bottom instead, so a door's texture stays put while the door
// moves and a step's texture lines up with the floor rather than the ceiling. The row offset shifts
// whatever was chosen.
//
// It is one line of arithmetic and it is the single most common way a wall comes out wrong, because
// the two choices differ only where the texture does not exactly fill the part -- which is most
// walls in most maps, and none of the ones anybody checks first.
float ComputeTextureTop(float partTop, float partBottom, float texHeight, bool pegBottom,
	float rowOffset);

}} // namespace zx::surfaces

#endif // ZX_WALLUV_COMPUTE_H
