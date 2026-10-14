// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/lightband_compute.h"

namespace zx { namespace levelmesh {

float ComputeBandPlaneZ(const BandPlane &p, float x, float y)
{
	return p.ic * (-p.d - p.a * x - p.b * y);
}

BandPlane ComputeBandPlaneFromFixed(double a, double b, double ic, double d)
{
	const double unit = 65536.0;
	BandPlane p;
	p.a = (float)(a / unit);
	p.b = (float)(b / unit);
	p.ic = (float)(ic / unit);
	p.d = (float)(d / unit);
	return p;
}

void ComputeBandBottomAtEnds(const BandPlane &boundary, float x1, float y1, float x2, float y2, float out[2])
{
	const bool sloped = boundary.a != 0.f || boundary.b != 0.f;
	out[0] = ComputeBandPlaneZ(boundary, sloped ? x1 : x2, sloped ? y1 : y2);
	out[1] = ComputeBandPlaneZ(boundary, x2, y2);
}

}} // namespace zx::levelmesh
