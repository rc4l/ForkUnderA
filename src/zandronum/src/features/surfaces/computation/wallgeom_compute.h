// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a sidedef looks like, worked out from the map instead of watched from GL.
//
// Every surface in this renderer is currently CAPTURED: GL walks the BSP, builds a GLWall or a
// GLFlat, and the mesh copies what it produced. That is why walls and flats are two different things
// all the way down -- two capture paths, two caches, two stamps, two slot tables -- and why every
// feature is written twice and the halves drift. Batched walls lost their dynamic lights while flats
// kept theirs. The stale light index could only ever affect walls. Projected decals had to be taught
// floors separately from walls, and each side broke the other twice.
//
// The split is not in the mesh -- a MeshPiece is already a surface, whatever made it -- and it is not
// really in Doom's data either: a sidedef part and a sector plane are both "geometry, a material, a
// light level, a colormap". It is in the derivation. So the way to one surface type is to derive the
// geometry here, from sides and sectors, rather than to keep two transcriptions of what GL does.
//
// This is the first piece of that: the heights of the three parts of a sidedef. It is deliberately
// the smallest thing that is worth testing on its own, because GLWall::Process is a thousand lines of
// accumulated cases and the way to replace it is one answerable question at a time, not a rewrite
// that has to be right everywhere before it can be run once.
//
// Engine-free: fixed-point heights in, quad extents out. No seg, no sector, no renderer.

#ifndef ZX_WALLGEOM_COMPUTE_H
#define ZX_WALLGEOM_COMPUTE_H

namespace zx { namespace surfaces {

// The four plane heights a sidedef sits between, in map units.
//
// A one-sided wall has no back sector: pass the same heights for both and only the middle part comes
// back, spanning the whole opening.
struct WallHeights
{
	float frontFloor, frontCeiling;
	float backFloor,  backCeiling;
	bool  twoSided;
	// [rc4l] Whether each plane is the sky, because two of GL's span rules ask.
	//
	// An upper between two sky ceilings is not drawn at all -- the sky itself covers that strip, and
	// drawing the sidedef's top texture there paints a wall over the horizon. The front floor's
	// clamp on an upper is likewise skipped when both floors are sky.
	bool  frontCeilingSky, backCeilingSky;
	bool  frontFloorSky,   backFloorSky;
};

// One drawable part of a sidedef: the vertical span it covers.
//
// `top` is always the higher number. An empty part -- a step that goes the wrong way, a ceiling that
// does not step down -- comes back with top <= bottom and `present` false, which is a different thing
// from a part that is present and zero-height, and the difference has bitten before: a wall of zero
// height still has a texture and still registers, and a wall that is absent must not.
struct WallPart
{
	float bottom, top;
	bool  present;
};

// [rc4l] The upper and lower of a wall, asked about BOTH ENDS AT ONCE.
//
// Two-ended because GL's clamps are: the front floor cuts the bottom off an upper only when it does
// so at both ends, and the front ceiling cuts the top off a lower only when it does so at both ends.
// A wall that pinches out at one end is still one quad, and GL moves the whole quad or neither end
// of it. Asking each end separately gives a different answer on every sloped wall, and dbab04 --
// 337 sloped pieces -- is where that shows.
//
// The parts come back whether or not they have area: a sloped wall can exist at one end and not the
// other, and GL draws it when EITHER end has area. Deciding that is the caller's job.
void ComputeUpperSpan(const WallHeights &a, const WallHeights &b, WallPart &pa, WallPart &pb);
void ComputeLowerSpan(const WallHeights &a, const WallHeights &b, WallPart &pa, WallPart &pb);
// The middle part.
//
// On a one-sided wall this is the whole thing, floor to ceiling. On a two-sided line it is the
// OPENING -- the gap between the highest floor and the lowest ceiling -- which is where a middle
// texture hangs, and it is empty when the sectors do not overlap at all (a closed door seen from
// outside).
WallPart ComputeMiddlePart(const WallHeights &h);

// [rc4l] What a two-sided middle texture is CLIPPED to, which is not the opening.
//
// ComputeMiddleTexturePart hangs the texture. This decides what survives, and DoMidTexture decides it
// by asking which of the four planes would leave an artefact -- a question whose answer flips on
// whether the sidedef has an upper or a lower texture at all, and on whether the ceilings are sky:
//
//   top, no upper texture:  both ceilings sky -> no clip at all; otherwise the HIGHER ceiling
//   top, with upper:        the back ceiling where the two cross; otherwise the LOWER ceiling
//   bottom, no lower:       the LOWER floor
//   bottom, with lower:     the back floor where the two cross; otherwise the HIGHER floor
//
// "The opening" -- lower ceiling to higher floor -- is only the last case of each. Using it
// everywhere cut 128-unit grates down to 96 on dbab02 and clipped a 235-unit one to 96, because a
// line with no upper texture is clipped to the ceiling ABOVE, not the one below.
struct MidTextureClip
{
	float texTop, texBottom;   // where the hanging texture is; the same at both ends
	bool  hasUpper, hasLower;  // does the sidedef carry a texture in that slot
	bool  frontCeilingSky, backCeilingSky;
	bool  wrap;                // ML_WRAP_MIDTEX or WALLF_WRAP_MIDTEX: the texture repeats, so no clip
	bool  clipToPlanes;        // false when both sides are the same sector and nothing forces it
};

void ComputeMiddleClip(const WallHeights &a, const WallHeights &b, const MidTextureClip &c,
	WallPart &pa, WallPart &pb);

// [rc4l] Where a wall that pinches out actually ENDS -- horizontally, not just vertically.
//
// A sloped wall can have area at one end and none at the other. It is tempting to hand that to the
// rasteriser as a triangle -- two corners coincident -- and that is what this derivation did. GL does
// something else: SetWallCoordinates finds the point along the wall where the top meets the bottom,
// moves that END of the wall there, and moves the horizontal texture coordinate with it. The result
// is a narrower QUAD, and its `u` at the cut end is nowhere near the linedef's.
//
// So the difference was never in the texture coordinate rule. It was that GL's wall is shorter than
// the linedef and ours was not: 33 pieces on dbab04 and 2 on dbab01, every one of them a wall that
// pinches out, and no ladder had ever compared a horizontal coordinate to notice.
//
// Both ends cannot pinch -- a wall with no area at either end is not drawn at all -- so the two
// fractions are independent and the original endpoints are the right reference for both.
struct WallPinch
{
	float fracLeft, fracRight;   // where along the linedef each end now sits, 0..1
	float ztop[2], zbottom[2];   // with the pinched end collapsed to the intersection height
};

void ComputeWallPinch(const float *ztopIn, const float *zbottomIn, WallPinch &out);

}} // namespace zx::surfaces

#endif // ZX_WALLGEOM_COMPUTE_H
