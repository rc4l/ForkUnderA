// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/decalorder_compute.h"

namespace zx { namespace hwrender {



static bool IsAdditive(const TranslucentDraw &d) { return d.blend == 2; }

bool ComputeDrawsBefore(const TranslucentDraw &a, const TranslucentDraw &b)
{
	// [rc4l] A decal is a STAGE, not a distance. This is the whole of the fix.
	//
	// GL never sorts a decal against anything: its renderer draws each wall's decals as passengers of
	// the wall they are glued to, walking the sidedef's attached list, in a pass that finishes before
	// sprites begin. Position in the order comes from what a thing IS.
	//
	// Reconstructing that from distance is what has failed three times, and it fails in a way that
	// looks like bad luck rather than a wrong rule: two coplanar quads differ in distance only by
	// where each centre happens to fall, so which of a scorch and a glow lands second depends on the
	// geometry of the pair. Answering it for the pair on screen -- a nudge, an exact-equality
	// tie-break, a coincidence radius -- fixes that pair and leaves the next one. Continuous fire
	// found the next one immediately: a scorch from one bolt overlapping the glow of another, too far
	// apart to be called coincident, ordered by distance, and eating a dark ring out of the middle of
	// the flash.
	//
	// So the stage decides, and inside it the order is GL's: additive last, because additive blending
	// only brightens and so can only be lost by being buried, then oldest first, which is the
	// attachment order GL walks.
	const bool aDecal = a.decal, bDecal = b.decal;
	if (aDecal != bDecal) return aDecal;

	if (aDecal)
	{
		if (IsAdditive(a) != IsAdditive(b)) return !IsAdditive(a);
		return a.first < b.first;
	}

	// Everything else is back to front, as it was.
	if (a.distSq != b.distSq) return a.distSq > b.distSq;

	// [rc4l] At equal distance, additive still draws last, and then the buffer offset decides.
	//
	// std::sort is not stable, so two draws at one distance traded places between frames and
	// flickered through each other. Falling back to creation order makes equal distances resolve the
	// same way every frame.
	if (IsAdditive(a) != IsAdditive(b)) return !IsAdditive(a);
	return a.first < b.first;
}

} }   // namespace zx::hwrender
