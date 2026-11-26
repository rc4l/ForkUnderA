// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/blockmap-filter/computation/cellarrays_compute.h"
#include <cstddef>

namespace zx { namespace cells {

void ComputeCellLink(std::vector<ThingEntry> &cell, const ThingEntry &e)
{
	cell.push_back(e);
}

bool ComputeCellUnlink(std::vector<ThingEntry> &cell, const void *node)
{
	for (size_t i = cell.size(); i-- > 0; )
	{
		if (cell[i].node == node)
		{
			cell.erase(cell.begin() + (std::ptrdiff_t)i);
			return true;
		}
	}
	return false;
}

bool ComputeCellRefresh(std::vector<ThingEntry> &cell, const void *node, int64_t x, int64_t y, int64_t radius, int64_t attackRadius)
{
	for (ThingEntry &e : cell)
	{
		if (e.node == node)
		{
			e.x = x;
			e.y = y;
			e.radius = radius;
			e.attackRadius = attackRadius;
			return true;
		}
	}
	return false;
}

bool ComputeBoxMissesLine(const LineEntry &l, int64_t left, int64_t right, int64_t bottom, int64_t top)
{
	return right <= l.left || left >= l.right || top <= l.bottom || bottom >= l.top;
}

bool ComputeSightLineFits(int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t dx, int64_t dy)
{
	return Fits32(x1) && Fits32(y1) && Fits32(x2) && Fits32(y2) && dx == x2 - x1 && dy == y2 - y1;
}

}} // namespace zx::cells
