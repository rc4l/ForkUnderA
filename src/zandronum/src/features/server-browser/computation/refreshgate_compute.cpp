// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/refreshgate_compute.h"

namespace zx
{

RefreshGateOut GateRefresh(const RefreshGateIn &in)
{
	RefreshGateOut out;

	if (in.minIntervalMs <= 0)
		return out;

	// Never refreshed. The floor measures time since the last sweep, and there has not been one.
	if (in.msSinceLastRefresh < 0)
		return out;

	if (in.msSinceLastRefresh >= in.minIntervalMs)
		return out;

	out.allowed = false;

	// Rounding UP, so the number is never smaller than the wait it describes. It cannot come out as
	// zero without a clamp: the three returns above have already taken every case where the floor is
	// met, so `left` is at least 1 here.
	const int left = in.minIntervalMs - in.msSinceLastRefresh;
	out.waitSeconds = (left + 999) / 1000;

	return out;
}

} // namespace zx
