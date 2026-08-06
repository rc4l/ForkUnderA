// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/choicerow_compute.h"

namespace zx
{

ChoiceCell ChoiceCellAt(int index, int count, int x, int totalWidth, int gap)
{
	ChoiceCell out;

	if ((count <= 0) || (index < 0) || (index >= count))
		return out;
	if (gap < 0)
		gap = 0;

	const int gaps = gap * (count - 1);
	const int usable = totalWidth - gaps;
	if (usable <= 0)
		return out;

	const int each = usable / count;
	if (each <= 0)
		return out;

	out.x = x + index * (each + gap);

	// The remainder goes to the last cell, so the row ends exactly where it was told to rather than a
	// pixel or two short -- which is visible against a panel edge.
	out.width = (index == count - 1) ? (totalWidth - (out.x - x)) : each;
	out.valid = true;
	return out;
}

int ChoiceHitTest(int px, int count, int x, int totalWidth, int gap)
{
	for (int i = 0; i < count; ++i)
	{
		const ChoiceCell cell = ChoiceCellAt(i, count, x, totalWidth, gap);
		if (!cell.valid)
			continue;

		if ((px >= cell.x) && (px < cell.x + cell.width))
			return i;
	}

	return -1;
}

int ChoiceStep(int selected, int count, int step)
{
	if (count <= 0)
		return 0;

	int at = ChoiceNormalise(selected, count) + step;

	// Clamped, not wrapped -- see the header. Stopping at the end is what lets the same arrow key
	// carry on out of the row without changing the answer on its way past.
	if (at < 0)
		at = 0;
	if (at >= count)
		at = count - 1;

	return at;
}

int ChoiceNormalise(int selected, int count)
{
	if (count <= 0)
		return 0;

	return ((selected < 0) || (selected >= count)) ? 0 : selected;
}

} // namespace zx
