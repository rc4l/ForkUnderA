// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/net-client/computation/serverfloor_compute.h"

#include <cstdint>

namespace zx { namespace netclient {

int64_t ComputeServerHeldFloorZ(int64_t boxFloorz, int64_t z, bool consolePlayer)
{
	if (consolePlayer || boxFloorz <= z)
		return boxFloorz;
	return z;
}

bool ComputeRereadFloorFromLines(bool checkPositionGood, bool consolePlayer)
{
	return !checkPositionGood && !consolePlayer;
}

}} // namespace zx::netclient
