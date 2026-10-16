// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/planeuv.h"

#include "m_fixed.h"
#include "tables.h"

namespace zx { namespace surfaces {

void PlaneUVFor(const GLSectorPlane &plane, zx::materials::MaterialRef mat, zx::levelmesh::PlaneUVTransform &out)
{
	out = zx::levelmesh::ComputeIdentityPlaneUV();
	out.xoffs = FIXED2FLOAT(plane.xoffs);
	out.yoffs = FIXED2FLOAT(plane.yoffs);
	out.xscale = FIXED2FLOAT(plane.xscale);
	out.yscale = FIXED2FLOAT(plane.yscale);
	out.angleDegrees = ANGLE2FLOAT(plane.angle);
	int w, h;
	zx::materials::MaterialRenderSize(mat, w, h);
	out.texWidth = (float)w;
	out.texHeight = (float)h;
	out.hasCanvas = zx::materials::MaterialIsCanvas(mat);
}

}} // namespace zx::surfaces
