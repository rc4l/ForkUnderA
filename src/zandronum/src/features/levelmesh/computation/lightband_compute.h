// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] 3D-floor light banding, moved from geometry into the shader (plan phase P2a).
//
// Today GLWall::SplitWall (gl/scene/gl_walls.cpp) splits a wall into a separate GLWall per
// 3D-floor light band, and splits it *again* left-to-right wherever a sloped band plane crosses it
// diagonally. That makes a side's vertex count depend on
// frontsector->e->XFloor.lightlist.Size(), which P_Recalculate3DFloors rebuilds every frame for any
// sector with a moving plane -- so the count is not a load-time constant, and a fixed vertex range
// is impossible. See docs/levelmesh-PLAN.md, Resolved section 3.
//
// Evaluating the band per fragment removes both splits: vertical banding becomes a lookup, and a
// sloped band is just a plane evaluated at the pixel. The band planes are packed exactly the way
// FRenderState::SetGlowPlanes already packs secplane_t -- (a, b, ic, d) as floats -- so the shader
// side reuses a convention that is already proven in main.vp.
//
// Header-pure and engine-free so the selection rule is unit-tested off-engine; the GL plumbing stays
// thin glue around it.

#ifndef ZX_LIGHTBAND_COMPUTE_H
#define ZX_LIGHTBAND_COMPUTE_H

namespace zx { namespace levelmesh {

// One band boundary, packed like a glow plane: secplane_t's a, b, ic and d as floats.
struct BandPlane
{
	float a, b, ic, d;
};

// Plane height at a world point. Mirrors secplane_t::ZatPoint -- ic * (-d - a*x - b*y) -- which is
// also what main.vp computes for the glow planes, so CPU and GPU agree by construction.
float ComputeBandPlaneZ(const BandPlane &p, float x, float y);

// Which light band a point falls in.
//
// lightlist is sorted top to bottom, and band i spans from plane[i] down to plane[i+1]; the last
// band runs to negative infinity. A point at or above plane[0] is band 0. The caller maps the
// returned index straight onto lightlist[i], with no special case for the topmost band: when
// lightlist[0] is the sector's own light, GLWall::Put3DWall already leaves the level untouched
// because p_lightlevel aliases the sector's, so band 0 needs no separate rule here.
//
// Returns 0 for an empty or null list, so a caller with no 3D floors reads the same as a caller
// whose pixel sits in the top band.
int ComputeLightBandIndex(const BandPlane *planes, int count, float x, float y, float up);

// The shader carries a fixed-size band array; a sector with more bands than this cannot be
// expressed per-fragment and has to keep the old geometry split.
const int kMaxLightBands = 16;

// Bands actually uploadable for a lightlist of this size, clamped to the array and never negative.
int ComputeUploadableBandCount(int listSize);

// Whether this lightlist still needs GLWall::SplitWall. True only past the array limit -- the case
// the fixed-range budget cannot cover, so it is worth counting rather than silently degrading.
bool ComputeNeedsGeometrySplit(int listSize);

}} // namespace zx::levelmesh

#endif // ZX_LIGHTBAND_COMPUTE_H
