// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/computation/continuelist_compute.h"

namespace zx
{

namespace
{

int Clamped(int value, int count)
{
	if (value < 0)
		return 0;
	if (value >= count)
		return count - 1;
	return value;
}

} // namespace

int StepContinueList(ContinueListKey key, int selected, int count, int visibleRows)
{
	if (count <= 0)
		return 0;

	// A selection from before the list changed shape. Pulled in first, so every key below is asked
	// about a row that exists.
	selected = Clamped(selected, count);

	// A page is a screenful, and a viewport reported as holding nothing still has to move somewhere.
	const int page = (visibleRows > 0) ? visibleRows : 1;

	switch (key)
	{
	case ContinueListKey::Up:
		return (selected > 0) ? (selected - 1) : (count - 1);

	case ContinueListKey::Down:
		return (selected < count - 1) ? (selected + 1) : 0;

	case ContinueListKey::PageUp:
		return Clamped(selected - page, count);

	case ContinueListKey::PageDown:
		return Clamped(selected + page, count);

	case ContinueListKey::Home:
		return 0;

	case ContinueListKey::End:
		break;
	}

	return count - 1;
}

int ComputeContinueVisibleRows(int listHeight, int rowHeight)
{
	if (rowHeight <= 0)
		return 1;

	const int rows = listHeight / rowHeight;
	return (rows > 0) ? rows : 1;
}

} // namespace zx
