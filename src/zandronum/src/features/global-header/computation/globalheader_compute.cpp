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
	m.labelPad = 12;

	m.glowInset = 5;
	m.glowRadius = 12;

	// [rc4l] Both are room for the ORB, not taste, and both are the SMALLEST value that is room for
	// it. The marker hangs five units to the left of the tab it is on and is twelve across, so 17 is
	// the point where it stops overlapping whatever is to its left: the screen edge for the first
	// tab, the previous pill for the rest. The old 6 looked better and drew the marker on top of the
	// tab before it, and the old 10 drew the first one half off the side of the screen.
	m.leftPad = m.glowInset + m.glowRadius;
	m.gap = m.glowInset + m.glowRadius;

	// A quarter larger than the size everything above is written at. Turn this one number to resize
	// the bar, its tabs, their labels and the orb together; nothing else needs touching.
	m.zoomPercent = 125;

	// Enough that the bar and the menu read as two separate things. A title touching the bar makes
	// the bar look like part of the menu, which is the one thing it must never look like: it is the
	// same bar over every screen.
	m.menuGap = 12;
	return m;
}

int MenuClearanceY(const HeaderMetrics &m)
{
	// Two conversions in one, both rounded UP because half a pixel of overlap is still overlap.
	//
	// The halving is the change of space, the bar's units down to the stock menus' 320x200. The zoom
	// is the dial: a bar drawn a quarter larger reaches a quarter further down the screen, and a
	// clearance that did not know that would put the menus back under it. That is the whole reason
	// the dial lives in the metrics rather than in the drawing code.
	const int zoom = (m.zoomPercent > 0) ? m.zoomPercent : 100;
	return ((m.barH + m.menuGap) * zoom + 199) / 200;
}

int HeaderRowWidth(const HeaderMetrics &m, const int *labelWidths, int count)
{
	if ((labelWidths == 0) || (count <= 0))
		return 0;

	int w = 0;
	for (int i = 0; i < count; ++i)
	{
		w += labelWidths[i] + 2 * m.labelPad;
		if (i > 0)
			w += m.gap;
	}

	return w;
}

int HeaderRowLeft(const HeaderMetrics &m, const int *labelWidths, int count)
{
	// Centred on the bar once the caller has said how wide the bar is. Never closer to the edge than
	// leftPad, which is not spare margin but the room the first tab's focus orb needs: a row that is
	// too wide to centre should crowd the middle, not push its own marker off the screen.
	if (m.barW <= 0)
		return m.leftPad;

	const int left = (m.barW - HeaderRowWidth(m, labelWidths, count)) / 2;
	return (left > m.leftPad) ? left : m.leftPad;
}

HeaderRect HeaderTabRect(const HeaderMetrics &m, const int *labelWidths, int count, int index)
{
	if ((labelWidths == 0) || (index < 0) || (index >= count))
		return HeaderRect();

	// Walk the earlier pills rather than storing offsets: the row is two or three items long, and a
	// cached layout is a thing that can disagree with the widths it was built from.
	int x = HeaderRowLeft(m, labelWidths, count);
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
