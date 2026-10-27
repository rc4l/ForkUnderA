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

}} // namespace zx::hwrender
