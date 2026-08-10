// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "globalheader_compute.h"

namespace zx
{

HeaderMetrics DefaultHeaderMetrics()
{
	HeaderMetrics m;
	m.barH = 26;
	m.tabTop = 5;
	m.tabH = 16;
	m.leftPad = 10;
	m.gap = 6;
	m.labelPad = 12;

	// Enough that the bar and the menu read as two separate things. A title touching the bar makes
	// the bar look like part of the menu, which is the one thing it must never look like: it is the
	// same bar over every screen.
	m.menuGap = 12;
	return m;
}

int MenuClearanceY(const HeaderMetrics &m)
{
	return (m.barH + m.menuGap + 1) / 2;
}

HeaderRect HeaderTabRect(const HeaderMetrics &m, const int *labelWidths, int count, int index)
{
	if ((labelWidths == 0) || (index < 0) || (index >= count))
		return HeaderRect();

	// Walk the earlier pills rather than storing offsets: the row is two or three items long, and a
	// cached layout is a thing that can disagree with the widths it was built from.
	int x = m.leftPad;
	for (int i = 0; i < index; ++i)
		x += labelWidths[i] + 2 * m.labelPad + m.gap;

	return HeaderRect(x, m.tabTop, labelWidths[index] + 2 * m.labelPad, m.tabH);
}

int HeaderTabAtPoint(const HeaderMetrics &m, const int *labelWidths, int count, int px, int py)
{
	for (int i = 0; i < count; ++i)
	{
		const HeaderRect r = HeaderTabRect(m, labelWidths, count, i);
		if ((r.w > 0) && (px >= r.x) && (px < r.x + r.w) && (py >= r.y) && (py < r.y + r.h))
			return i;
	}

	return -1;
}

bool HeaderBarContains(const HeaderMetrics &m, int py)
{
	return (py >= 0) && (py < m.barH);
}

int StepHeaderTab(int index, int count, int step)
{
	if (count <= 0)
		return 0;

	int next = index + step;
	if (next < 0)
		next = 0;
	if (next >= count)
		next = count - 1;

	return next;
}

bool CursorAtTopRow(const bool *selectable, int count, int selected)
{
	if (selectable == 0 || count <= 0 || selected < 0 || selected >= count)
		return false;

	// A cursor parked on something unreachable is not "at the top", whatever its index. It can
	// happen: an entry greys itself out while the cursor is sitting on it.
	if (!selectable[selected])
		return false;

	for (int i = 0; i < selected; ++i)
	{
		if (selectable[i])
			return false;
	}

	return true;
}

} // namespace zx
