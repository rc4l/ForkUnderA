// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-session/computation/joinupdate_compute.h"

#include "features/fixed64/computation/net_fixed_wire_compute.h"

namespace zx { namespace netsession {

bool ComputeSpawnRoundsPosition(int64_t x, int64_t y, int64_t z)
{
	return zx::WireRoundtripShort(x) != x || zx::WireRoundtripShort(y) != y || zx::WireRoundtripShort(z) != z;
}

}} // namespace zx::netsession
