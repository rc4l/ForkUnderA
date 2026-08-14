// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/pillgrid_compute.h"

namespace zx
{

namespace
{

// Where a pill's middle sits along its own line, measured from the line's left edge. Pills are laid
// left to right with one gap between each, so the offset is the widths before it plus the gaps.
int CentreOnLine(const WadListLine &line, const std::vector<int> &widths, int gap, size_t index)
{
	int at = 0;

	for (size_t i = line.first; i < index; ++i)
	{
		if (i < widths.size())
			at += widths[i] + gap;
	}

	return at + ((index < widths.size()) ? (widths[index] / 2) : 0);
}

} // namespace

PillMove MovePillVertically(const WadListLayout &layout, const std::vector<int> &widths,
                            int gap, int index, int dir)
{
	PillMove out;

	if ((dir == 0) || (index < 0) || (layout.lines.size() < 2))
		return out;

	// Which line the pill is on. A pill the layout does not hold cannot move within it.
	size_t line = layout.lines.size();
	for (size_t i = 0; i < layout.lines.size(); ++i)
	{
		if ((static_cast<size_t>(index) >= layout.lines[i].first) &&
			(static_cast<size_t>(index) < layout.lines[i].end))
		{
			line = i;
			break;
		}
	}

	if (line >= layout.lines.size())
		return out;

	// No line that way: the key belongs to whatever is above or below the axis.
	if ((dir < 0) && (line == 0))
		return out;
	if ((dir > 0) && (line + 1 >= layout.lines.size()))
		return out;

	const size_t target = (dir < 0) ? (line - 1) : (line + 1);
	const WadListLine &to = layout.lines[target];

	if (to.first >= to.end)
		return out;		// an empty line has nothing to land on

	const int want = CentreOnLine(layout.lines[line], widths, gap, static_cast<size_t>(index));

	// [rc4l] Nearest by CENTRE, not by position in the line. Pills are different widths, so matching
	// by index would send the marker sideways while the player pressed a key pointing straight down.
	size_t best = to.first;
	int bestGap = -1;

	for (size_t i = to.first; i < to.end; ++i)
	{
		const int at = CentreOnLine(to, widths, gap, i);
		const int away = (at > want) ? (at - want) : (want - at);

		if ((bestGap < 0) || (away < bestGap))
		{
			bestGap = away;
			best = i;
		}
	}

	out.leaves = false;
	out.index = static_cast<int>(best);
	return out;
}

} // namespace zx
