// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Telling a double-click from two clicks, since the menu system delivers single clicks only.
#ifndef ZX_DOUBLECLICK_COMPUTE_H
#define ZX_DOUBLECLICK_COMPUTE_H

namespace zx
{

// [Doruk-Aksoy] The last completed click: what it landed on and when, in menu tics.
struct ClickClock
{
	int target;
	int time;

	ClickClock() : target(-1), time(-1000) {}
};

// [Doruk-Aksoy] Records a click on `target` at `now` and says whether it completes a double, which then disarms so a third click starts afresh.
bool ComputeDoubleClick(ClickClock &clock, int target, int now, int windowTics);

} // namespace zx

#endif // ZX_DOUBLECLICK_COMPUTE_H
