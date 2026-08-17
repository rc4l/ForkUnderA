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

// [rc4l] How far THROUGH its surface a decal's box should reach.
//
// This is the room the mark has to carry round a corner. Past a join a decal continues for at most
// its own remaining width or height before the unwrap runs out of texture, so the box is sized from
// the mark: smaller cuts the wrap short, larger only reaches surfaces the unwrap then discards.
//
// The floor of 24 is for a small graphic on a flat: a bullet hole is a few units across, and its box
// still has to be deep enough to stay on a floor that steps or slopes underneath it. At eight, marks
// on anything but dead-flat ground came out clipped.
float ComputeDecalBoxDepth(float halfW, float halfH);

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

// [rc4l] The texture coordinate, UNWRAPPED around whatever corner the surface turns at.
//
// MUST match fuaDecalUV in dgscene.cpp, which is the same arithmetic transcribed into GLSL. The
// shader cannot be called from here, so this is the specification and that is the transcription;
// changing one without the other is the failure this file exists to make loud.
//
// `local` is the point in box units (from ComputeDecalLocal) and `nrm` is the surface's unit normal,
// which the shader reconstructs from the derivatives of the world position.
//
// The idea: `local.z` is how far the surface has moved through the plane the decal was shot at, and
// around a corner that distance is exactly the distance travelled along the new surface away from
// the join. Adding it to whichever texture axis the surface turned about continues the picture
// across the join at its own scale -- seamless and unstretched -- and it costs nothing on the
// original surface, where that distance is zero.
//
// Returns false when the point is past the end of the picture, which the caller must DISCARD rather
// than clamp: clamping repeats the edge texel for ever, which is the dragged column again by another
// route.
bool ComputeDecalUnwrapUV(const DecalFrame &f, const float local[3], const float nrm[3],
                          float &outU, float &outV);

}} // namespace zx::levelmesh

#endif // ZX_DECALVOLUME_COMPUTE_H
