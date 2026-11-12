// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/scoreboardtop_compute.h"

namespace zx {

int ComputeScoreboardTop(int minYPos, int cleanYfac, int titleHeight)
{
	const int screenTall = 200;
	if (minYPos >= screenTall * cleanYfac && titleHeight >= screenTall)
		return 32 * cleanYfac;
	return minYPos;
}

} // namespace zx
