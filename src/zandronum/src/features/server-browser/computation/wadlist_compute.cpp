// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/wadlist_compute.h"

// [rc4l] size_t, which this file loops with -- see pillflow_compute.cpp for why <vector> is not
// enough on libstdc++.
#include <cstddef>

namespace zx
{

namespace
{

// What a file costs on a line: itself, plus the separator BEFORE it when it is not first.
int CostOf(const std::vector<int> &w, size_t i, bool bFirstOnLine, int sepWidth)
{
	return w[i] + (bFirstOnLine ? 0 : sepWidth);
}

} // namespace

WadListLayout LayoutWadList(const std::vector<int> &itemWidths, int sepWidth,
                            int ellipsisWidth, int maxWidth, int maxLines)
{
	WadListLayout out;

	if (itemWidths.empty())
		return out;

	size_t i = 0;
	while (i < itemWidths.size())
	{
		// One more line than the cap allows means everything left over is dropped, and the line we
		// already placed has to make room to say so.
		if ((maxLines > 0) && (out.lines.size() >= static_cast<size_t>(maxLines)))
		{
			out.truncated = true;
			break;
		}

		const size_t first = i;
		int used = 0;

		// Greedy: as many as fit. The first file on a line always goes on it even when it is wider
		// than the line, because the alternative is a layout that silently loses a file.
		do
		{
			used += CostOf(itemWidths, i, (i == first), sepWidth);
			++i;
		}
		while ((i < itemWidths.size()) &&
		       (used + CostOf(itemWidths, i, false, sepWidth) <= maxWidth));

		out.lines.push_back(WadListLine(first, i));
	}

	out.shown = out.lines.empty() ? 0 : out.lines.back().end;

	// [rc4l] The ellipsis has to FIT, which means taking files off the last line until it does. Doing
	// this after the fill rather than reserving room up front keeps the untruncated case exact: a
	// list that ends on the cap with nothing left over spends no width on a marker it will not draw.
	if (out.truncated && !out.lines.empty())
	{
		WadListLine &last = out.lines.back();

		int used = 0;
		for (size_t k = last.first; k < last.end; ++k)
			used += CostOf(itemWidths, k, (k == last.first), sepWidth);

		// Never take the only file on the line: a line showing nothing but "..." says less than a
		// line showing one name and "...", and the tooltip has the rest either way.
		while ((last.end > last.first + 1) && (used + ellipsisWidth > maxWidth))
		{
			--last.end;
			used -= CostOf(itemWidths, last.end, (last.end == last.first), sepWidth);
		}

		out.shown = last.end;
	}

	return out;
}

} // namespace zx
