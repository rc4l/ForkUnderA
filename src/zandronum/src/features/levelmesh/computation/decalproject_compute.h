// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The arithmetic of a PROJECTED MESH DECAL, engine-free so it can be tested.
//
// A mark is a box in the world with a picture in it. Whatever geometry is inside the box is clipped
// against it and gets that picture printed on it, along the direction the projectile was travelling.
// Nothing is glued to a sidedef, nothing "creeps" from surface to surface, and no fragment ever has
// to work out how far it is from an impact: a floor beneath a wall mark is simply more geometry in
// the same box, so it receives the same projection, continuous across the corner because it is one
// projection and not two decals meeting.
//
// This replaces a screen-space version that spent nine rounds of adjustment failing at corners. The
// lesson worth keeping is why it could not be adjusted into working: it measured a walk ALONG the
// geometry from the impact, so the floor's share of a fixed-size mark was `extent - height above
// the floor`, and no correction to the walk can hand out more than the extent has. Projection has
// no such budget -- the box decides what is inside it, and a surface's share is however much of it
// is in there.
//
// Everything here is in MAP space: x east, y north, z up, angles in Doom's convention. The mesh's
// (x, z-up, y) is a rendering detail applied at emit time.
//
// The three failures this file exists to keep out of the renderer:
//
//   * a degenerate basis  -- Doom hands you a missile with velz == 0 even when the player is aiming
//                            at the floor, because autoaim never sets it. Projecting along that
//                            velocity onto a floor is projecting PARALLEL to the surface: the
//                            picture is infinitely long and nothing sensible is drawn.
//   * a mark on the back  -- a surface facing away from the projectile cannot have been sprayed by
//                            it. Skip the check and a pillar prints a second, mirrored copy of the
//                            mark on the face nobody shot at.
//   * a grazing smear     -- a rocket flying nearly parallel to a wall projects a mark stretched by
//                            1/cos(angle), which goes to infinity. Real, but unusable at the limit.

#ifndef ZX_DECALPROJECT_COMPUTE_H
#define ZX_DECALPROJECT_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] The projection volume. Local coordinates are (right, up, axis) about `origin`.
//
// `axis` points the way the projectile was GOING, so it points INTO the surface. A surface is lit by
// this box the way a slide projector at `origin - axis*near` lights a wall.
struct DecalBox
{
	float origin[3];
	float right[3];    // the picture's +u direction, unit, perpendicular to axis
	float up[3];       // the picture's +v direction, unit, perpendicular to axis and right
	float axis[3];     // direction of travel, unit
	float halfW;       // half the picture's width, along right
	float halfH;       // half the picture's height, along up
	float near_;       // how far BACK from origin the box reaches, along -axis
	float far_;        // how far FORWARD from origin the box reaches, along +axis
};

// [rc4l] Build the picture's axes from how the projectile was moving.
//
// `vel` is the velocity at the moment of impact -- which must be captured before P_ExplodeMissile
// zeroes it. `surfN` is the normal of the surface that stopped it, pointing out of that surface
// towards the projectile.
//
// The axis is the direction of travel, so an oblique hit leaves an oblique mark and a head-on hit
// leaves a round one, for free and without a single tuned constant. Two cases are not free:
//
//   * No usable velocity (a missile with velz == 0 landing on a floor, or an explosion with no
//     direction at all). The projection falls back to -surfN, which is the head-on case and is what
//     every glued-quad decal has always done.
//   * A grazing hit. `maxSkewCos` is the smallest allowed cosine between -axis and surfN; below it
//     the axis is tilted back towards the surface normal until it is exactly at the limit, which
//     keeps the mark oblique without letting it stretch without bound.
//
// Returns false and leaves a head-on basis when the velocity was unusable, so the caller can tell
// the two apart rather than inferring it.
bool BuildDecalBasis(const float vel[3], const float surfN[3], float maxSkewCos,
                     float outRight[3], float outUp[3], float outAxis[3]);

// [rc4l] Can this surface have been sprayed from that direction?
//
// dot(n, axis) is negative when the surface faces the incoming projectile. `minFacing` is how
// square-on it has to be: 0 accepts everything up to exactly edge-on, which lets a wall perfectly
// parallel to the projection contribute a zero-area sliver of stretched texture. A small positive
// value drops those.
bool AcceptSurfaceForDecal(const float n[3], const float axis[3], float minFacing);

// [rc4l] Where the box is centred, given where Doom stopped the projectile.
//
// A missile is an AXIS-ALIGNED BOX in Doom, not a point, so it explodes with its centre up to
// `radius` short of whatever it hit -- and the line that stopped it is not always the face you can
// see: a rocket clipping the corner of a pillar is blocked by the pillar's side while the face under
// the crosshair is its front. Advancing the origin along the direction of travel by the radius puts
// the box ON the geometry instead of short of it, and a box does not care which sidedef it lands on:
// it prints on whatever is inside it. That is the whole reason the "no decal at connecting lines"
// case disappears rather than being special-cased.
void DecalOriginFromImpact(const float pos[3], const float axis[3], float radius, float outOrigin[3]);

// [rc4l] How deep the box has to be, which is not a free choice.
//
// A tilted projection lands its picture on a SLANTED band of depth: move one unit up the picture and
// the surface it lands on is tan(theta) further away, where theta is the angle between the
// projection and the surface it hit. Over the whole picture that band is size*tan(theta) either side
// of the contact point, so a box any shallower SLICES ITS OWN MARK with a straight edge -- which is
// what a hard-edged wedge of scorch beside a corner turns out to be, every time.
//
// `spreadFraction` is the extra reach on the near side, as a fraction of the picture's size: the
// depth a square-on hit needs in order to carry onto the floor in front of the wall or round a
// corner, where the slant demands nothing. The far side gets only the slant plus a small margin,
// because the only thing that side does is decide whether a mark prints through a thin wall into
// the next room.
//
// `cosTheta` is the cosine between the projection and the surface normal, clamped by the caller's
// skew limit; zero would be a projection running exactly along the surface, which has no finite
// answer, so it is floored here rather than trusted.
void ComputeDecalBoxDepth(float size, float cosTheta, float spreadFraction,
                          float &outNear, float &outFar);

// [rc4l] Clip a convex polygon to the box, in box-local coordinates.
//
// Input is world-space points; output is (u, v, w) triples where u and v are along right and up and
// w is along axis, all relative to origin. Sutherland-Hodgman against the six slabs, so the result
// stays convex and the caller can fan-triangulate it.
//
// Returns the number of output points, or 0 if the polygon is entirely outside. `outXYZW` must have
// room for at least (count + 6) * 3 floats: each plane can add at most one vertex.
int ClipPolygonToDecalBox(const float *worldPoly, int count, const DecalBox &box,
                          float *outLocal, int maxOut);

// [rc4l] The picture coordinate of a box-local point. 0..1 across the box, whatever it landed on.
void DecalUV(const float local[3], const DecalBox &box, float &u, float &v);

// [rc4l] Mirror the picture without moving it.
//
// DECALDEF's randomflipx/randomflipy mirror the graphic. Flipping by negating the offset of the
// quad instead -- which an earlier version did -- MOVES the mark to the other side of the impact,
// so a BFG's scorch and its glow, flipped independently, ended up as two marks side by side. Doing
// it in the texture coordinate cannot move anything.
void DecalFlipUV(bool flipX, bool flipY, float &u, float &v);

}} // namespace zx::levelmesh

#endif // ZX_DECALPROJECT_COMPUTE_H
