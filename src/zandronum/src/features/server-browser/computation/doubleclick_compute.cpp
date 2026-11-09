// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/server-browser/computation/doubleclick_compute.h"

namespace zx
{

bool ComputeDoubleClick(ClickClock &clock, int target, int now, int windowTics)
{
	const bool bDouble = (target >= 0) && (target == clock.target) && ((now - clock.time) <= windowTics);

	if (bDouble)
	{
		clock = ClickClock();
		return true;
	}

	clock.target = target;
	clock.time = now;
	return false;
}

} // namespace zx
