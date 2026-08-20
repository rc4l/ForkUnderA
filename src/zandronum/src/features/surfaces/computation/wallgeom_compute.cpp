// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/computation/wallgeom_compute.h"

namespace zx { namespace surfaces {

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
	//
	// [rc4l] ...and it cannot start below the back FLOOR. A sector squeezed shut -- a door mid-move,
	// a crusher down, a lift whose ceiling has dropped past its floor -- reports a ceiling BELOW its
	// own floor, and taking that literally hangs the upper texture down through the doorway into
	// space the lower texture already covers. GL clamps; so does this.
	//
	// Found by fua_surface_verify rather than by looking: 28 pieces on dbab01 and 13 on dbab04
	// disagreed with the capture, all of them uppers starting too low, all by exactly the distance
	// from the back ceiling up to the back floor.
	float bottom = (h.backCeiling > h.backFloor) ? h.backCeiling : h.backFloor;
	// [rc4l] ...nor below the FRONT floor, for the same reason from the other side: the wall the
	// player is looking at starts at the floor they are standing on. dbab04 has uppers whose back
	// ceiling sits at -8 against a front floor of 0, and the capture starts them at 0.
	if (bottom < h.frontFloor) bottom = h.frontFloor;
	return Make(bottom, h.frontCeiling);
}

WallPart ComputeLowerPart(const WallHeights &h)
{
	if (!h.twoSided) return Make(0.f, 0.f);
	// The mirror of the clamp above: a lower texture stops at the back ceiling when the sector
	// behind has closed past it, rather than continuing up through geometry that is no longer there.
	float top = (h.backFloor < h.backCeiling) ? h.backFloor : h.backCeiling;
	// ...and not above the front ceiling, the mirror of the clamp in ComputeUpperPart.
	if (top > h.frontCeiling) top = h.frontCeiling;
	return Make(h.frontFloor, top);
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

WallPart ComputeMiddleTexturePart(const WallHeights &h, float texHeight, bool pegBottom,
	float rowOffset)
{
	const WallPart opening = ComputeMiddlePart(h);
	if (!opening.present || texHeight <= 0.f) return opening;
	if (!h.twoSided) return opening;   // a one-sided wall really is floor to ceiling

	// [rc4l] A middle texture on a two-sided line HANGS. It does not fill the opening.
	//
	// It is placed by its own height and its pegging -- from the top of the opening downward, or
	// from the bottom upward when the line is unpegged-bottom -- shifted by the sidedef's row
	// offset, and then clipped to the opening. Treating it as "the gap" is how a 8-unit grate came
	// out 208 units tall: fua_surface_verify found 93 pieces on dbab02 exactly that way, every one a
	// two-sided middle, every one the full opening instead of its own texture.
	float bottom, top;
	if (pegBottom)
	{
		bottom = opening.bottom + rowOffset;
		top = bottom + texHeight;
	}
	else
	{
		top = opening.top + rowOffset;
		bottom = top - texHeight;
	}

	// Clipped to the opening: the part hanging outside it is not drawn, and a texture entirely
	// outside leaves nothing at all.
	if (bottom < opening.bottom) bottom = opening.bottom;
	if (top > opening.top) top = opening.top;
	return WallPart{ bottom, top, top > bottom };
}

bool ComputeSideHasGeometry(const WallHeights &h)
{
	return ComputeUpperPart(h).present || ComputeLowerPart(h).present || ComputeMiddlePart(h).present;
}

}} // namespace zx::surfaces
