// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The geometry of a projected decal: its box, its orientation, and the unwrap around a corner.
//
// A projected decal is a box in the world that paints whatever surface the depth buffer puts inside
// it. Everything about where that paint lands is arithmetic, and every piece of it has already been
// wrong once in a way no assertion caught and no screenshot settled quickly:
//
//   - the box was too shallow, and marks came out clipped on anything but flat ground;
//   - the centre ignored the graphic's own offsets, so an offset decal landed half a graphic away;
//   - the wall normal was taken from the linedef without regard to which side the decal stuck to;
//   - and the unwrap was a plain projection, which drags one column of texels along any surface that
//     turns edge-on -- the rocket-scorch streak, the BFG smear, the stretched bullet hole.
//
// Each of those is a pure function of numbers. They live here so they can be argued with directly
// rather than through a rebuild and a walk back to the same corner of the same map.

#ifndef ZX_DECALVOLUME_COMPUTE_H
#define ZX_DECALVOLUME_COMPUTE_H

namespace zx { namespace levelmesh {

// The box's orientation, with each axis ALREADY divided by its own half-extent.
//
// Pre-dividing is what makes the inside test one dot product per axis: `dot(P - centre, axis)` comes
// out in -1..1 across the box with nothing left to divide. The shader relies on it, so it is part of
// the contract rather than an optimisation applied later.
struct DecalFrame
{
	float u[3];   // across the surface
	float v[3];   // up the surface
	float n[3];   // through the surface
};

// [rc4l] How far the blast reaches, which must never be near enough to cut the picture.
//
// The sphere exists to bound how far through SPACE a mark carries, not to shape it -- the graphic's
// own alpha does that. Sized at the picture's half-width it cut the corners off, because a rectangle's
// corner is further from its centre than its edge is, and a scorch came out as a disc with a hard rim
// stamped through it. The diagonal is the least that cannot, and half again on top leaves room to
// reach a floor or a wall standing off to the side of the impact without clipping that either.
float ComputeDecalReach(float halfW, float halfH);

// Build a frame from unit axes and the box's three half-extents. Returns false, leaving the frame
// untouched, if any extent is zero or negative -- a degenerate box would divide by zero and paint
// the whole screen.
bool ComputeDecalBasis(const float axisU[3], const float axisV[3], const float axisN[3],
                       float halfW, float halfH, float halfDepth, DecalFrame &out);

// [rc4l] A wall's axes, from the linedef it is stuck to.
//
// `along` is the linedef's direction; `normal` is the quarter turn of it that points OUT of the face
// the decal stuck to. Which quarter turn depends on the side: a linedef's front side faces to the
// right of v1->v2, so the back side takes the other one. Getting this backwards points the box into
// the wall instead of out of it, and the decal lands on whatever is behind it.
//
// Returns false for a zero-length linedef, which a malformed map can contain.
bool ComputeWallDecalAxes(float dx, float dy, bool backSide, float along[2], float normal[2]);

// [rc4l] From a decal's ANCHOR to the centre of its box, along the surface.
//
// The engine positions a decal by the point its graphic hangs from, not by the middle of it, and the
// two coincide only when the graphic's offsets happen to sit at its middle. gl_decal.cpp works the
// left edge out as `anchor - leftOffset` and the top as `anchor + topOffset`; these are the middle of
// that. `leftOffset`/`topOffset` are already scaled, as the half-extents are.
//
// A flipped graphic is drawn mirrored, so its offset is measured from the other edge.
float ComputeDecalAlongOffset(float halfW, float leftOffset, bool flipX);
float ComputeDecalUpOffset(float halfH, float topOffset, bool flipY);

// Where a world point falls in the box, in -1..1 per axis. Outside that range on any axis, the point
// is not in the box at all.
void ComputeDecalLocal(const DecalFrame &f, const float rel[3], float local[3]);

// [rc4l] The mark's texture coordinate on WHATEVER surface a fragment turns out to be on.
//
// MUST match fuaDecalUV in dgscene.cpp, which is the same arithmetic transcribed into GLSL. The
// shader cannot be called from here, so this is the specification and that is the transcription;
// changing one without the other is the failure this file exists to make loud.
//
// `rel` is the fragment's offset from where the blast landed and `nrm` is the surface's own normal,
// which the shader reads out of the depth buffer. Neither involves the camera, so neither can make a
// mark change as the camera moves.
//
// The idea, and it is the whole design: a blast radiates from a POINT, so every surface it reaches is
// measured in its own plane from that point. The mark's across-axis is turned into the surface to
// keep the picture the right way up, the perpendicular completes the pair, and the coordinate is just
// the offset along those two at the mark's own scale.
//
// What that buys is the absence of special cases. Projecting from a plane instead means choosing an
// axis before the surface is known, which works on the surface that was hit and degenerates on
// everything else -- a floor met at a right angle has no movement along the projection axis at all,
// so a row of texels is dragged across it. Four attempts to patch around that each broke somewhere
// new: the drag, then a black slab where the drag covered a whole box, then a hole where the slab was
// refused, then a wedge of floor a corner-patching strip never reached. Measuring from the point has
// none of them, and no surface in range can be missed, because being in range is the only condition.
//
// Returns false when the fragment is past the edge of the picture, which the caller must DISCARD
// rather than clamp -- clamping repeats the edge texel for ever, which is a dragged row by another
// route.
bool ComputeDecalSurfaceUV(const DecalFrame &f, const float rel[3], const float nrm[3],
                           float &outU, float &outV);

}} // namespace zx::levelmesh

#endif // ZX_DECALVOLUME_COMPUTE_H
