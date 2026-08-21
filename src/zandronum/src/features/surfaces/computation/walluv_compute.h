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

// [rc4l] Where the texture is pegged from -- GL's own form, because guessing at it cost two rounds.
//
// Doom aligns a wall texture from a REFERENCE PAIR, not from the part being drawn. Each part names
// its own pair: an upper references the front ceiling and the back ceiling, a lower references the
// back floor and the front floor, a one-sided middle references the front ceiling and floor -- and
// all of them use the plane's TEXTURE Z rather than where the plane currently is, so a moving
// sector slides its geometry without sliding its picture.
//
// Unpegged, the texture hangs from refCeiling. Pegged, it is pushed down so its last row lands on
// refFloor, which is the shift GL writes as `texHeight - (refSpan + vOffset)`.
//
// vOffset is the sky special case: two sky ceilings meeting over a lower texture reference it
// against the sky instead. Zero everywhere else.
//
// Derived by reading DoTexture rather than by inferring it from pictures. The two inferred versions
// before this were 91.9% and 55.7% against the capture -- both plausible, both wrong, and the
// second one looked like progress until it was measured.
float ComputeTextureTop(float refCeiling, float refFloor, float texHeight, bool pegged,
	float rowOffset, float vOffset);


// [rc4l] CheckTexturePosition's shift is no longer a function here.
//
// BuildDerivedWallSpan applies it inline -- subtract floor(min(uplft.v, uprgt.v)) from all four --
// because having it as a separate step is what let the shift land in the shipping derivation while
// the ladder went on scoring a copy that did not apply it. It is stated at that call site now, and
// what checks it is fua_surface_verify against the capture on real maps.

// Whether GL will clamp this wall vertically, given the four corner v values AFTER the shift. A wall
// that occupies exactly one copy of its texture is clamped so that filtering cannot bleed the
// opposite edge in -- the seam at the top of a door being the case everyone has seen.
bool ComputeWallClampsY(float vUpLeft, float vUpRight, float vLoLeft, float vLoRight);

}} // namespace zx::surfaces

#endif // ZX_WALLUV_COMPUTE_H
