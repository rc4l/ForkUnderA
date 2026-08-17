// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The decisions the flat mesh makes about a surface, pulled out where they can be tested.
//
// Every one of these shipped wrong at least once, and each failure was silent and total rather than
// noisy and partial -- which is exactly the shape a unit test catches and a screenshot does not:
//
//   * winding    -- a flat wound the wrong way for the side it is viewed from is DELETED by back-face
//                   culling. Getting this wrong removed every ceiling in the level, and getting it
//                   wrong a second way removed every 3D floor's walkable top surface. Neither
//                   produced an error; both produced a room with a hole in it.
//   * blend      -- a translucent surface classified opaque renders solid. A grate over a lava pit
//                   came out as solid metal.
//   * overlap    -- two coplanar surfaces claiming the same area do not agree on depth to the last
//                   bit, so the rasteriser stipples between them. 1799 such pairs existed before
//                   anyone noticed, because the artefact is only visible at certain angles.
//
// Engine-free and header-pure: flatmesh.cpp and the fua_mesh_verify diagnostic are the glue.

#ifndef ZX_FLATMESH_COMPUTE_H
#define ZX_FLATMESH_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] Must this flat's triangles be wound in reverse?
//
// A subsector's vertices arrive in one fixed order, so a floor and a ceiling built from them have
// the SAME winding while facing opposite directions. Exactly one of the two has to be reversed or a
// single cull mode deletes it.
//
// The argument is which SIDE the surface is viewed from, not which way its plane's normal points.
// Those differ: a 3D floor's walkable top surface is the control sector's ceiling plane, so its
// normal points down while the surface is seen from above. Winding by the normal looked more
// principled and culled exactly those surfaces.
bool ComputeFlatWindingReversed(bool viewedFromBelow);

// [rc4l] Which blend mode a surface with this render style and alpha needs.
//
//   0 opaque / alpha-tested   1 normal translucent   2 additive
//
// The alpha threshold is one 8-bit step: Doom expresses translucency in 0..255, so anything at or
// above 255/256 is opaque and rounding it any other way makes fully-opaque surfaces take the sorted
// translucent path for nothing.
int ComputeSurfaceBlendMode(bool additive, float alpha);

// [rc4l] Which blend mode a WALL needs.
//
// A wall's own alpha is not the whole story, and using it alone drew a pane of frosted glass as a
// solid white slab. A two-sided middle texture carries its transparency in the TEXTURE's alpha
// channel while the wall's alpha stays 1, and the alpha-tested opaque pass turns every partially
// transparent texel fully opaque. What actually knows is the draw list the engine routed the wall
// into: GLDL_TRANSLUCENT means "this needs blending", whatever the numbers say.
//
// Same lesson as the flat winding: use the decision the engine already made rather than re-deriving
// it from the inputs it made it from.
int ComputeWallBlendMode(bool inTranslucentList, bool additive, float alpha);

// [rc4l] Which blend mode a render STYLE needs -- sprites, decals, anything with an FRenderStyle.
//
// The full style matrix resolves to GL blend enums, which would then have to be mapped back into
// another API's. These four cases are what Doom content actually uses: opaque/masked, normal
// translucency, additive (plasma, fireballs, explosions, scorch decals) and the fuzz shadow.
// Anything exotic lands on normal translucency -- wrong, but visible rather than invisible.
int ComputeStyleBlendMode(bool shadow, bool additive, float alpha);

// [rc4l] Twice the signed area of a triangle projected onto the horizontal plane.
//
// Positive and negative correspond to the two winding directions; which one is "front" is a
// rasteriser convention and deliberately NOT decided here. What matters, and what the verifier
// checks, is that surfaces viewed from the same side all agree.
//
// Returns 0 for a degenerate triangle -- squashed pieces are legitimate (a retired range is zeroed
// in place) and must not be read as a winding error.
float ComputeTriangleWindingZ(float ax, float ay, float bx, float by, float cx, float cy);

// An axis-aligned bound, in map units. Degenerate on the axis a flat surface is flat in.
struct MeshBox
{
	float x0, x1, y0, y1, z0, z1;
};

// [rc4l] Do these two surfaces lie in the same plane AND claim overlapping area in it?
//
// Both conditions are needed. Two surfaces that merely touch along an edge are ordinary level
// geometry -- every floor in a level shares edges with its neighbours -- so the overlap has to be
// wide in at least two axes before it counts. Without that, the predicate flags most of the map and
// reports nothing.
bool ComputeCoplanarOverlap(const MeshBox &a, const MeshBox &b, float eps);

// [rc4l] Is the winding convention applied consistently across the level?
//
// Given, for surfaces viewed from above and from below, how many wind each way: the convention holds
// when each group is internally unanimous AND the two groups are opposite. Anything else means
// back-face culling keeps some surfaces and drops others that should be equally visible.
//
// A group with no members at all is vacuously fine -- a level with no ceilings is not a bug.
bool ComputeWindingConsistent(int fromAbovePositive, int fromAboveNegative,
                              int fromBelowPositive, int fromBelowNegative);

// [rc4l] Everything gl_SetPlaneTextureRotation applies to a flat's texture coordinates.
//
// The mesh baked UVs as (x/64, -y/64) and stopped there, which is only correct for an unoffset,
// unscaled, unrotated 64x64 flat. GL builds a texture matrix from all five of these, so scrolling
// floors did not scroll -- and, less visibly and far more widely, every flat whose texture is not
// 64x64 tiled at the wrong rate, because the 64/size term compensating for that lives in the same
// matrix. Nobody reported the second one; it reads as "the texture looks slightly off".
struct PlaneUVTransform
{
	float xoffs, yoffs;        // map units; what a scroller animates
	float xscale, yscale;      // 1 = unscaled
	float angleDegrees;        // the plane's own angle, NOT pre-negated
	float texWidth, texHeight; // in texels; the 64x64 assumption this replaces
	bool  hasCanvas;           // a camera texture is stored upside down, so its V scale is negated
};

// A transform that changes nothing, for a plain 64x64 flat.
PlaneUVTransform ComputeIdentityPlaneUV();

// [rc4l] Map a world position to texture coordinates for a flat.
//
// Mirrors gl_SetPlaneTextureRotation's matrix exactly, including its order. That matrix is built by
// post-multiplication, so the transforms apply to the coordinate in the REVERSE of the order they
// are written: rotate, then the 64/size correction, then the offset, then the plane's own scale.
// Applying them in the written order looks equally reasonable and puts scrolled or rotated flats in
// the wrong place.
void ComputePlaneUV(float px, float py, const PlaneUVTransform &t, float &u, float &v);

}} // namespace zx::levelmesh

#endif // ZX_FLATMESH_COMPUTE_H
