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

int ComputePuffSend(bool spawnState, bool hitThingSeeSound, bool attackSound, bool temporaryObituary)
{
	if (!spawnState) return kPuffNoNetID;
	if (hitThingSeeSound || attackSound || temporaryObituary) return kPuffWithNetID;
	return kPuffEachClient;
}

bool ComputeSkipsPuffFor(bool isShooter, bool noNetId, bool clientSidePuffs)
{
	return isShooter && noNetId && clientSidePuffs;
}

uint32_t ComputeClientAppliedFlags(uint32_t current, uint32_t incoming, uint32_t clientOwned)
{
	return (incoming & ~clientOwned) | (current & clientOwned);
}

bool ComputeJoinSendsTranslation(uint32_t translation, uint32_t classTranslation)
{
	return translation != classTranslation;
}

}} // namespace zx::netreplication
