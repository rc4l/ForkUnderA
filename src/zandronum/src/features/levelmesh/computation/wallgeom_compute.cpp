// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/wallgeom_compute.h"

namespace zx { namespace levelmesh {

namespace {

WallPart Make(float bottom, float top)
{
	WallPart p;
	p.bottom = bottom;
	p.top = top;
	// [rc4l] Strictly greater. A part whose top meets its bottom covers nothing, and registering it
	// puts a zero-area quad in the mesh with a real material and a real slot -- which is a draw call
	// and a batch for a surface nobody can see.
	p.present = (top > bottom);
	return p;
}

} // namespace

WallPart ComputeUpperPart(const WallHeights &h)
{
	if (!h.twoSided) return Make(0.f, 0.f);
	// The back ceiling has to be lower than the front's, or there is no wall above the opening.
	return Make(h.backCeiling, h.frontCeiling);
}

WallPart ComputeLowerPart(const WallHeights &h)
{
	if (!h.twoSided) return Make(0.f, 0.f);
	return Make(h.frontFloor, h.backFloor);
}

WallPart ComputeMiddlePart(const WallHeights &h)
{
	if (!h.twoSided) return Make(h.frontFloor, h.frontCeiling);

	// The opening: between the higher floor and the lower ceiling. Both sectors have to agree there
	// is a gap, which is what makes a closed door produce nothing here.
	const float floor = (h.frontFloor > h.backFloor) ? h.frontFloor : h.backFloor;
	const float ceiling = (h.frontCeiling < h.backCeiling) ? h.frontCeiling : h.backCeiling;
	return Make(floor, ceiling);
}

bool ComputeSideHasGeometry(const WallHeights &h)
{
	return ComputeUpperPart(h).present || ComputeLowerPart(h).present || ComputeMiddlePart(h).present;
}

}} // namespace zx::levelmesh
