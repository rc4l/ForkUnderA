// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "menu/restartnote_compute.h"

#include <cstring>

namespace zx {

RestartNoteFit ComputeRestartNote(const char *value, const char *launched, bool recorded, bool grayed,
	int valueRight, int gap, int rightEdge, int scale, const int *widths, int count)
{
	const RestartNoteFit none = { -1, 0 };
	if (!recorded || grayed || value == nullptr || launched == nullptr || strcmp(value, launched) == 0)
		return none;
	for (int s = scale; s >= 1; --s)
		for (int i = 0; i < count; ++i)
			if (valueRight + (gap + widths[i]) * s <= rightEdge)
				return { i, s };
	return none;
}

} // namespace zx
