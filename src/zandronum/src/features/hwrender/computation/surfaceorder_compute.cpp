// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/surfaceorder_compute.h"
#include "features/hwrender/computation/radixsort_compute.h"

#include <algorithm>
#include <cstddef>
#include <string.h>
#include <vector>

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

	// [rc4l] Then the plane layers, farthest first, a plane ahead of what shares its layer: a medkit under water is drawn before the water over it.
	if (a.layer != b.layer) return a.layer > b.layer;
	if (a.plane != b.plane) return a.plane;

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

TranslucentSortKey ComputeTranslucentSortKey(const TranslucentDraw &d)
{
	TranslucentSortKey k;
	k.lo = d.first;
	const unsigned long long additive = IsAdditive(d) ? 1ull : 0ull;
	// [rc4l] Decals are a stage ahead of everything (the top bit clear), ordered additive last then oldest first.
	if (d.decal)
	{
		k.hi = additive << 62;
		return k;
	}
	// [rc4l] Then farther plane layers first, a plane ahead of its layer, farther first, additive last; a non-negative float's bits rise with it, so their complement falls.
	unsigned bits;
	memcpy(&bits, &d.distSq, sizeof(bits));
	const unsigned long long layer = (unsigned long long)(0xffff - (d.layer < 0 ? 0 : d.layer > 0xffff ? 0xffff : d.layer));
	k.hi = (1ull << 63) | (layer << 47) | ((d.plane ? 0ull : 1ull) << 46) | ((unsigned long long)(~bits) << 14) | (additive << 13);
	return k;
}

void ComputeSortTranslucentOrder(std::vector<TranslucentOrder> &keys, std::vector<TranslucentHalf> &halves, std::vector<TranslucentHalf> &scratch, std::vector<TranslucentOrder> &moved)
{
	// [Doruk-Aksoy] Sorted by the high half alone, so the low half's passes and moving whole keys are spared, then each run of equal high halves put in its low halves' order.
	const size_t n = keys.size();
	halves.resize(n);
	for (size_t i = 0; i < n; i++) halves[i] = TranslucentHalf{ keys[i].key.hi, (unsigned)i };
	ComputeRadixSort<8>(halves, scratch, [](const TranslucentHalf &h, int b) { return (unsigned)(h.hi >> (8 * b)) & 0xFF; });
	for (size_t lo = 0, hi = 0; lo < n; lo = hi)
	{
		for (hi = lo + 1; hi < n && halves[hi].hi == halves[lo].hi; ) hi++;
		// [Doruk-Aksoy] An insertion sort, stable and quick on the runs of one or two that ties make.
		for (size_t k = lo + 1; k < hi; k++)
		{
			const TranslucentHalf h = halves[k];
			size_t j = k;
			for (; j > lo && keys[h.place].key.lo < keys[halves[j - 1].place].key.lo; j--) halves[j] = halves[j - 1];
			halves[j] = h;
		}
	}
	moved.resize(n);
	for (size_t k = 0; k < n; k++) moved[k] = keys[halves[k].place];
	keys.swap(moved);
}

int ComputeBlendLayers(const BlendSpan *spans, int n, float viewZ, int *layers, bool *planes)
{
	std::vector<float> heights;
	for (int i = 0; i < n; i++)
	{
		planes[i] = spans[i].surface && spans[i].lo == spans[i].hi;
		if (planes[i] && std::find(heights.begin(), heights.end(), spans[i].lo) == heights.end())
			heights.push_back(spans[i].lo);
	}
	for (int i = 0; i < n; i++)
	{
		int layer = 0;
		for (size_t k = 0; k < heights.size(); k++)
		{
			const float h = heights[k];
			// [rc4l] A floor seen from above hides what is below it; a ceiling seen from below, what is above.
			const bool beyond = (viewZ >= h) ? (spans[i].hi <= h && spans[i].lo < h)
			                                 : (spans[i].lo >= h && spans[i].hi > h);
			if (beyond) layer++;
		}
		layers[i] = layer;
	}
	return (int)heights.size();
}

bool ComputePiecesBefore(const ScenePiece &a, const ScenePiece &b)
{
	// Correctness first: blended after opaque, always.
	const int ba = (a.blendMode != 0), bb = (b.blendMode != 0);
	if (ba != bb) return ba < bb;
	// Then batching: identical state adjacent, so it draws once.
	if (a.material != b.material) return a.material < b.material;
	return a.baseTex < b.baseTex;
}

bool ComputeGLSpritesBefore(const GLSpriteOrder &a, const GLSpriteOrder &b, bool compatSpriteSort)
{
	if (a.depth != b.depth) return a.depth > b.depth;   // farthest first
	// [rc4l] The tie-break flips with the compat flag, and it is not cosmetic: maps built against the
	// older behaviour stack overlapping sprites the other way round, and getting it backwards puts the
	// wrong one in front in exactly the scenes that were authored to look right.
	return compatSpriteSort ? (a.spawnIndex < b.spawnIndex) : (a.spawnIndex > b.spawnIndex);
}

} }   // namespace zx::hwrender
