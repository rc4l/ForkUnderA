// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/dynlayout_compute.h"

namespace zx { namespace hwrender {

unsigned ComputeDynLayout(const int *order, int count, const unsigned *vertices, int recordsLeft, std::vector<DynSlot> &out)
{
	out.clear();
	unsigned total = 0;
	int records = 0;
	for (int i = 0; i < count; i++)
	{
		const int piece = order[i];
		if (vertices[piece] == 0) continue;
		const DynSlot s = { piece, total, vertices[piece], records < recordsLeft ? records++ : -1 };
		out.push_back(s);
		total += s.count;
	}
	return total;
}

}} // namespace zx::hwrender
