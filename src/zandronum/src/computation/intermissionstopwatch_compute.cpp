// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/intermissionstopwatch_compute.h"

namespace zx {

long ComputeClientStopWatch(long stopWatch, bool demoPaused)
{
	return (demoPaused == false && stopWatch > 0) ? stopWatch - 1 : stopWatch;
}

} // namespace zx
