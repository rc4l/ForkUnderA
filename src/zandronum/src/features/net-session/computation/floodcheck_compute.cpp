// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-session/computation/floodcheck_compute.h"

namespace zx { namespace net {

bool ComputeTimeInstancesFlood(const int (&oldestFirst)[TIME_INSTANCES], int gametic)
{
	// [rc4l] The window for each earlier instance, the oldest's the longest.
	static const int intervalTicks[TIME_INSTANCES] = { 105, 42, 7 };
	for (int i = TIME_INSTANCES - 1; i >= 0; i--)
	{
		if (oldestFirst[i] > 0 && gametic - oldestFirst[i] <= intervalTicks[i])
			return true;
	}
	return false;
}

}} // namespace zx::net
