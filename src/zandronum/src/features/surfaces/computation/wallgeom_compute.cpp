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

void ComputeUpperSpan(const WallHeights &a, const WallHeights &b, WallPart &pa, WallPart &pb)
{
	if (!a.twoSided) { pa = Make(0.f, 0.f); pb = Make(0.f, 0.f); return; }
	float bottomA = a.backCeiling, bottomB = b.backCeiling;
	// [rc4l] The FRONT FLOOR obstructs the bottom of an upper, and only when it does so at BOTH ends.
	//
	// This is `if (ffh1>bch1 && ffh2>bch2) { bch1a=ffh1; bch2a=ffh2; }` -- GL's own comment says
	// "the back sector's floor", and GL's own code says ffh, which is the front's. The plane matters:
	// a sector squeezed shut, a door mid-move, a crusher down, reports a ceiling BELOW the floor, and
	// taking that literally hangs the upper down through the doorway into space the lower already
	// covers. Clamping to the BACK floor instead is the same answer whenever the two floors agree --
	// which is every unsloped door, which is why it went unnoticed -- and a different one the moment
	// either floor slopes.
	//
	// Both ends together, not each on its own, because a wall that pinches out at one end is still
	// one quad and GL clamps it as one quad or not at all.
	if (a.frontFloor > bottomA && b.frontFloor > bottomB)
	{
		bottomA = a.frontFloor;
		bottomB = b.frontFloor;
	}
	pa = Make(bottomA, a.frontCeiling);
	pb = Make(bottomB, b.frontCeiling);
}

void ComputeLowerSpan(const WallHeights &a, const WallHeights &b, WallPart &pa, WallPart &pb)
{
	if (!a.twoSided) { pa = Make(0.f, 0.f); pb = Make(0.f, 0.f); return; }
	float topA = a.backFloor, topB = b.backFloor;
	// The mirror, and the same correction: `if (fch1<bfh1 && fch2<bfh2) { bfh1=fch1; bfh2=fch2; }`.
	// The FRONT CEILING cuts the top off a lower -- which matters most under a sky, where the back
	// sector's floor can stand well above the ceiling the player is looking through.
	if (a.frontCeiling < topA && b.frontCeiling < topB)
	{
		topA = a.frontCeiling;
		topB = b.frontCeiling;
	}
	pa = Make(a.frontFloor, topA);
	pb = Make(b.frontFloor, topB);
}

// [rc4l] The one-ended forms, which are the two-ended ones asked about a wall that does not slope.
//
// Kept because most of a level is flat and asking one question is clearer than asking the same
// question twice, and because a clamp that reads "both ends" is not wrong on a wall whose two ends
// are the same height -- it is simply the same test twice.
WallPart ComputeUpperPart(const WallHeights &h)
{
	WallPart a, b;
	ComputeUpperSpan(h, h, a, b);
	return a;
}

WallPart ComputeLowerPart(const WallHeights &h)
{
	WallPart a, b;
	ComputeLowerSpan(h, h, a, b);
	return a;
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

void ComputeMiddleClip(const WallHeights &a, const WallHeights &b, const MidTextureClip &c,
	WallPart &pa, WallPart &pb)
{
	float topA, topB, botA, botB;
	if (!c.clipToPlanes)
	{
		// Both sides the same sector, nothing forcing it: GL does not clip at all, because clipping
		// to planes that are the same plane can only produce artefacts.
		topA = topB = c.texTop;
		botA = botB = c.texBottom;
	}
	else
	{
		if (!c.hasUpper)
		{
			// An intra-sky line with no upper does not clip the texture at all.
			if (c.frontCeilingSky && c.backCeilingSky) { topA = topB = c.texTop; }
			else
			{
				// Missing texture: the HIGHER ceiling, and let the geometry clip what extrudes.
				topA = (a.backCeiling > a.frontCeiling) ? a.backCeiling : a.frontCeiling;
				topB = (b.backCeiling > b.frontCeiling) ? b.backCeiling : b.frontCeiling;
			}
		}
		else if ((a.backCeiling > a.frontCeiling || b.backCeiling > b.frontCeiling) &&
		         (!c.frontCeilingSky || c.backCeilingSky))
		{
			// The ceilings cross: use the back sector's and let the front's plane clip the polygon.
			topA = a.backCeiling; topB = b.backCeiling;
		}
		else
		{
			topA = (a.backCeiling < a.frontCeiling) ? a.backCeiling : a.frontCeiling;
			topB = (b.backCeiling < b.frontCeiling) ? b.backCeiling : b.frontCeiling;
		}

		if (!c.hasLower)
		{
			botA = (a.backFloor < a.frontFloor) ? a.backFloor : a.frontFloor;
			botB = (b.backFloor < b.frontFloor) ? b.backFloor : b.frontFloor;
		}
		else if (a.backFloor < a.frontFloor || b.backFloor < b.frontFloor)
		{
			botA = a.backFloor; botB = b.backFloor;
		}
		else
		{
			botA = (a.backFloor > a.frontFloor) ? a.backFloor : a.frontFloor;
			botB = (b.backFloor > b.frontFloor) ? b.backFloor : b.frontFloor;
		}

		// And then the texture itself clips, when it does not repeat: a hanging texture that ends
		// above the plane brings the polygon down with it. Both ends together, as everywhere else.
		if (!c.wrap)
		{
			if (c.texTop < topA && c.texTop < topB) topA = topB = c.texTop;
			if (c.texBottom > botA && c.texBottom > botB) botA = botB = c.texBottom;
		}
	}
	pa = Make(botA, topA);
	pb = Make(botB, topB);
}

bool ComputeSideHasGeometry(const WallHeights &h)
{
	return ComputeUpperPart(h).present || ComputeLowerPart(h).present || ComputeMiddlePart(h).present;
}

}} // namespace zx::surfaces
