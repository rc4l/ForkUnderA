// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/pillflow_compute.h"

namespace zx
{

std::vector<PillPlace> FlowPills(const std::vector<int> &widths, int contentWidth, int gap)
{
	std::vector<PillPlace> out;
	out.reserve(widths.size());

	if (contentWidth <= 0)
		contentWidth = 1;
	if (gap < 0)
		gap = 0;

	int x = 0;
	int row = 0;

	for (size_t i = 0; i < widths.size(); ++i)
	{
		const int w = (widths[i] > 0) ? widths[i] : 1;

		// Wrap when this one would run past the edge, but never wrap the FIRST pill of a row: it
		// would leave an empty row above it and then run past the edge anyway. A pill too wide for
		// the box gets its own row and is clipped where it is drawn.
		if ((x > 0) && (x + w > contentWidth))
		{
			++row;
			x = 0;
		}

		out.push_back(PillPlace(x, row, w));
		x += w + gap;
	}

	return out;
}

int PillFlowRowCount(const std::vector<PillPlace> &placed)
{
	int rows = 0;
	for (size_t i = 0; i < placed.size(); ++i)
	{
		if (placed[i].row + 1 > rows)
			rows = placed[i].row + 1;
	}

	return rows;
}

int PillFlowHitTest(const std::vector<PillPlace> &placed, int rowHeight, int x, int y)
{
	if (rowHeight <= 0)
		return -1;
	if (y < 0)
		return -1;

	const int row = y / rowHeight;

	for (size_t i = 0; i < placed.size(); ++i)
	{
		if (placed[i].row != row)
			continue;
		if ((x < placed[i].x) || (x >= placed[i].x + placed[i].width))
			continue;

		return static_cast<int>(i);
	}

	return -1;
}

} // namespace zx
