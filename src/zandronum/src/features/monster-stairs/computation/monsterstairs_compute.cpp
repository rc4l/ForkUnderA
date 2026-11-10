// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/monster-stairs/computation/monsterstairs_compute.h"

#include <cstdint>
#include <limits>

namespace zx { namespace monsterstairs {

Setting ComputeSettingFromBool(bool on)
{
	return on ? Setting::On : Setting::Off;
}

bool ComputeMonsterStairsEnabled(Setting map, Setting game, bool cvar)
{
	if (map != Setting::Unset)
		return map == Setting::On;
	if (game != Setting::Unset)
		return game == Setting::On;
	return cvar;
}

int64_t ComputeLineFloorDrop(int64_t bottom, int64_t lowfloor)
{
	const int64_t lo = std::numeric_limits<int64_t>::min(), hi = std::numeric_limits<int64_t>::max();
	if (bottom == lo || bottom == hi || lowfloor == lo || lowfloor == hi)
		return 0;
	return bottom >= lowfloor ? bottom - lowfloor : lowfloor - bottom;
}

bool ComputeDropoffBlocked(int64_t floorz, int64_t dropoffz, int64_t maxLineDrop, int64_t maxDropOff, bool stairs)
{
	int64_t drop = floorz - dropoffz;
	if (stairs && maxLineDrop < drop)
		drop = maxLineDrop;
	return drop > maxDropOff;
}

bool ComputeStairLift(int64_t z, int64_t boxFloorz, int64_t maxLineDrop, int64_t maxStepHeight)
{
	return boxFloorz > z && maxLineDrop <= maxStepHeight;
}

}} // namespace zx::monsterstairs
