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

// The width of one pill, label plus its padding either side.
static int PillWidth(const HeaderMetrics &m, int labelWidth)
{
	return labelWidth + 2 * m.labelPad;
}

int HeaderRowWidth(const HeaderMetrics &m, const int *labelWidths, int count, int pinnedIndex)
{
	if ((labelWidths == 0) || (count <= 0))
		return 0;

	// The pinned tab is not in the row, so it must not be measured as part of it -- otherwise the
	// centred pair are shoved off-centre by the width of a pill that is drawn somewhere else.
	int w = 0;
	bool bFirst = true;
	for (int i = 0; i < count; ++i)
	{
		if (i == pinnedIndex)
			continue;

		w += PillWidth(m, labelWidths[i]);
		if (bFirst == false)
			w += m.gap;
		bFirst = false;
	}

	return w;
}

int HeaderRowLeft(const HeaderMetrics &m, const int *labelWidths, int count, int pinnedIndex)
{
	// The floor the row may not cross. Normally that is leftPad, which is not spare margin but the
	// room the first tab's focus orb needs. With a tab pinned to the left it is that pill's right
	// edge plus a gap, so a wide centred row crowds the middle rather than sliding underneath the
	// pinned one -- two pills sharing pixels is a click that hits whichever was drawn last.
	int floorX = m.leftPad;
	if ((pinnedIndex >= 0) && (pinnedIndex < count) && (labelWidths != 0))
		floorX = m.leftPad + PillWidth(m, labelWidths[pinnedIndex]) + m.gap;

	if (m.barW <= 0)
		return floorX;

	const int left = (m.barW - HeaderRowWidth(m, labelWidths, count, pinnedIndex)) / 2;
	return (left > floorX) ? left : floorX;
}

HeaderRect HeaderTabRect(const HeaderMetrics &m, const int *labelWidths, int count, int index,
	int pinnedIndex)
{
	if ((labelWidths == 0) || (index < 0) || (index >= count))
		return HeaderRect();

	// The pinned tab has a home of its own at the left edge and never moves with the row.
	if (index == pinnedIndex)
		return HeaderRect(m.leftPad, m.tabTop, PillWidth(m, labelWidths[index]), m.tabH);

	// Walk the earlier pills rather than storing offsets: the row is two or three items long, and a
	// cached layout is a thing that can disagree with the widths it was built from.
	int x = HeaderRowLeft(m, labelWidths, count, pinnedIndex);
	for (int i = 0; i < index; ++i)
	{
		if (i == pinnedIndex)
			continue;
		x += PillWidth(m, labelWidths[i]) + m.gap;
	}

	return HeaderRect(x, m.tabTop, PillWidth(m, labelWidths[index]), m.tabH);
}

int HeaderTabAtPoint(const HeaderMetrics &m, const int *labelWidths, int count, int px, int py,
	int pinnedIndex)
{
	for (int i = 0; i < count; ++i)
	{
		const HeaderRect r = HeaderTabRect(m, labelWidths, count, i, pinnedIndex);
		if ((r.w > 0) && (px >= r.x) && (px < r.x + r.w) && (py >= r.y) && (py < r.y + r.h))
			return i;
	}

	return -1;
}

bool HeaderBarContains(const HeaderMetrics &m, int py)
{
	return (py >= 0) && (py < m.barH);
}

// [rc4l] Where a tab sits from the left, once the pinned one is counted first.
//
// The enum order and the drawn order disagree on purpose (see the header), so the arrows have to be
// told the drawn one or Left from the first centred tab would skip the pinned pill entirely and
// stop dead at the middle of the bar.
// Both of these are only ever reached with a pinned index in range: StepHeaderTabPinned turns the
// unpinned bar away before it gets here, so neither needs to guard against it again.
static int VisualPosition(int index, int pinnedIndex)
{
	if (index == pinnedIndex)
		return 0;

	return (index < pinnedIndex) ? index + 1 : index;
}

static int IndexAtVisualPosition(int position, int pinnedIndex)
{
	if (position == 0)
		return pinnedIndex;

	// Positions after the pinned one map back by skipping over it. The caller has already clamped
	// `position` into range, so shifted + 1 cannot run past the end.
	const int shifted = position - 1;
	return (shifted < pinnedIndex) ? shifted : shifted + 1;
}

int StepHeaderTabPinned(int index, int count, int pinnedIndex, int step)
{
	if (count <= 0)
		return 0;

	if ((pinnedIndex < 0) || (pinnedIndex >= count))
		return StepHeaderTab(index, count, step);

	int position = VisualPosition(index, pinnedIndex) + step;
	if (position < 0)
		position = 0;
	if (position >= count)
		position = count - 1;

	return IndexAtVisualPosition(position, pinnedIndex);
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
