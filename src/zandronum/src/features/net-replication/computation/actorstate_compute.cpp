// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-replication/computation/actorstate_compute.h"

#include <algorithm>

namespace zx { namespace netreplication {

bool ComputeSendsWoundedHealth(bool isPlayer, int health) { return !isPlayer && health > 0; }

bool ComputeAddPendingHealth(std::vector<int> &pending, int netId)
{
	if (netId <= 0 || std::find(pending.begin(), pending.end(), netId) != pending.end()) return false;
	pending.push_back(netId);
	return true;
}

int64_t ComputeSettledDeathZ(int64_t z, int64_t floorz, bool noGravity)
{
	return (!noGravity && z < floorz) ? floorz : z;
}

}} // namespace zx::netreplication
