// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/dynlayout_compute.h"
#include "features/hwrender/computation/spritepack_compute.h"

namespace zx { namespace hwrender {

DynLayoutTotals ComputeDynLayout(const int *order, int count, const unsigned *vertices, const bool *packed, int recordsLeft, std::vector<DynSlot> &out)
{
	out.clear();
	DynLayoutTotals t = { 0, 0, 0 };
	int records = 0;
	for (int i = 0; i < count; i++)
	{
		const int piece = order[i];
		if (vertices[piece] == 0) continue;
		DynSlot s = { piece, t.stream, vertices[piece], -1, -1, 0 };
		if (packed[piece]) s.sprite = t.sprites++;
		else
		{
			s.general = t.general;
			t.general += s.count;
			if (records < recordsLeft) s.record = records++;
		}
		out.push_back(s);
		t.stream += s.count;
	}
	return t;
}

unsigned ComputeDynWordsBound(unsigned pieces, unsigned vertices, unsigned matrices)
{
	return pieces * kSpriteWords + (vertices + (vertices + 1) / 2) * 6 + 4 + matrices * kModelMatrixWords;
}

float ComputeModelMatrixWord(unsigned matBase, int index)
{
	return index < 0 ? 0.f : (float)(matBase + (unsigned)index * kModelMatrixWords + 1);
}

unsigned ComputeDynWordsCapacity(unsigned need)
{
	const unsigned grown = need + need / 2;
	return grown > (1u << 20) ? grown : (1u << 20);
}

namespace {

// [Doruk-Aksoy] Whether `vertices`, a piece and a matrix every three of them, fit a storage buffer of `maxStorageBytes` at the capacity the stream would grow to.
bool DynVerticesFit(unsigned vertices, unsigned long long maxStorageBytes)
{
	const unsigned pieces = vertices / 3 + 1;
	return 4ull * ComputeDynWordsCapacity(ComputeDynWordsBound(pieces, vertices, pieces)) <= maxStorageBytes;
}

} // namespace

unsigned ComputeDynVertexCeiling(unsigned long long maxStorageBytes)
{
	if (!DynVerticesFit(0, maxStorageBytes)) return 0;
	unsigned lo = 0, hi = kDynVertexLimit;
	while (lo < hi)
	{
		const unsigned mid = lo + (hi - lo + 1) / 2;
		if (DynVerticesFit(mid, maxStorageBytes)) lo = mid;
		else hi = mid - 1;
	}
	return lo;
}

}} // namespace zx::hwrender
