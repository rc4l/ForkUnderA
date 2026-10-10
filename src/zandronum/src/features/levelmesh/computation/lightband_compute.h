// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A 3D-floor light band's boundary plane in map units, and where it cuts a wall: the numbers the map bake's SplitWall port reads.

#ifndef ZX_LIGHTBAND_COMPUTE_H
#define ZX_LIGHTBAND_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] One band boundary: secplane_t's a, b, ic and d as floats in map units.
struct BandPlane
{
	float a, b, ic, d;
};

// Plane height at a world point. Mirrors secplane_t::ZatPoint -- ic * (-d - a*x - b*y) -- which is
// also what main.vp computes for the glow planes, so CPU and GPU agree by construction.
float ComputeBandPlaneZ(const BandPlane &p, float x, float y);

// [rc4l] A secplane_t's raw 16.16 a, b, ic and d as a BandPlane in map units, so its height comes back in map units.
BandPlane ComputeBandPlaneFromFixed(double a, double b, double ic, double d);

// [rc4l] A band boundary at a wall's two ends, as GLWall::SplitWall reads it: sloped at each end, flat at the second end for both.
void ComputeBandBottomAtEnds(const BandPlane &boundary, float x1, float y1, float x2, float y2, float out[2]);

}} // namespace zx::levelmesh

#endif // ZX_LIGHTBAND_COMPUTE_H
