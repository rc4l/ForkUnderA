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
// [rc4l] How much of the blast is left after the walk to here, and where the picture is read.
//
// MUST match fuaDecalUV and fuaDecalReach in dgscene.cpp. Changing one without the other is the
// failure this file exists to make loud, and it has happened: this file described a SPHERE cut by a
// plane long after the shader had stopped doing that, so its tests passed while asserting behaviour
// nothing shipped.
//
// The model is a walk. A mark spreads from the one point it was made at, across whatever surfaces
// are in the way, so a fragment's coordinate is the distance the soot travelled to reach it. Two
// planes meet in a LINE and creep wraps around that line: the coordinate running along the corner
// carries straight over, and only the coordinate crossing it accumulates the journey -- down to the
// corner, then out from it.
//
// Measuring the walk radially instead funnels every path through one point, the impact's shadow on
// the surface, where a small patch of floor maps to a whole ring of the picture. It magnifies into
// spikes radiating from that point, which is what "the floor creep is oddly shrinked" was.
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

// [rc4l] Where a fragment reads the picture, and how far the soot walked to get there.
//
// `rel` is the fragment's offset from where the blast landed; `nrm` is the surface's own normal,
// read from the G-buffer. Neither involves the camera, so neither can make a mark change as the
// camera moves -- which it did, twice, when this was derived from the view.
//
// Three behaviours are load-bearing and each was wrong once:
//
//  - ALONG the corner carries over, ACROSS it accumulates. Feeding both to the same picture axis
//    turns the mark on its side as it wraps: a wall meeting a floor gives a horizontal corner, so
//    the walk crosses the picture vertically, and two walls meeting give a vertical one.
//
//  - the distance from the corner is ABSOLUTE, because the impact's own shadow lands exactly on it.
//    Dropping a perpendicular onto the surface cannot move along the hit plane's normal, so its foot
//    is always a point of the corner line. Signing it away from the hit plane sent the creep the
//    wrong way round a pillar, whose side faces run backwards from the corner and got nothing.
//
//  - going round the BACK of the corner costs the distance along it as well. A corner line is
//    infinite here and is not in the map -- it runs until the wall ends -- so charging only the
//    crossing let soot reach floor round the far side of a convex corner as cheaply as floor in
//    front of the wall, printing a second mirrored copy of the mark at full strength.
//
// Returns the picture coordinate in 0..1 through outU/outV and the walked distance through outPath.
// A coordinate outside 0..1 must be DISCARDED rather than clamped: clamping repeats the edge texel
// for ever, which is a dragged row by another route.
void ComputeDecalCreepUV(const DecalFrame &f, const float rel[3], const float nrm[3],
                         float &outU, float &outV, float &outPath);

// [rc4l] What is left of the blast after walking that far. The last of it fades rather than stopping,
// so a mark with picture left when it runs out of reach does not end on a line.
float ComputeDecalCreepReach(float path, float radius);

// [rc4l] How a decal's alpha changes with age, exactly as the engine's own fader does.
//
// A fader holds FULL alpha for decayStart tics and only then fades to nothing over decayTime --
// GoAway2, which the BFG glow and the plasma flare use, is 1 second then 3. Standing in for all of
// them with a single linear ramp from spawn ran a glow at two thirds brightness the instant it
// appeared and removed it while the engine still had a second left to draw, which beside GL reads as
// "the glow is much dimmer in Vulkan".
//
// decayTime <= 0 means the decal never fades, which is the case for every animator that is not a
// fader: stretchers, sliders and colour changers leave the alpha alone and never remove the mark.
float ComputeDecalFade(int spawnTic, int decayStart, int decayTime, int now);

// [rc4l] The light level a decal is SHADED at, which is not always its sector's.
//
// DECALDEF marks the glows fullbright, and gl_decal.cpp honours it by shading at 255 with no relative
// light -- while still fogging at the sector's own level, which is why this answers only the shading
// half. Shading a glow at sector light instead makes it vanish into a dark corridor.
int ComputeDecalShadeLight(bool fullbright, int sectorLight);

}} // namespace zx::levelmesh

#endif // ZX_DECALVOLUME_COMPUTE_H
