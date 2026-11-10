// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a server sends its clients of an actor that took damage.

#ifndef ZX_ACTORSTATE_COMPUTE_H
#define ZX_ACTORSTATE_COMPUTE_H

#include <cstdint>
#include <vector>

namespace zx { namespace netreplication {

// [rc4l] Whether a damaged actor's health goes to every client: a non-player still alive, since a player's goes with its own damage command and a death with the death.
bool ComputeSendsWoundedHealth(bool isPlayer, int health);

// [rc4l] Add a wounded actor's network id to the tic's pending list once, so a shotgun's seven pellets send one health; false for no id (a server-only actor) or one already pending.
bool ComputeAddPendingHealth(std::vector<int> &pending, int netId);

// [Doruk-Aksoy] The height a death is sent at: a walker killed after stepping onto a higher floor, before the next tic's P_ZMovement raised it there, stands below it, and that raise came after the send, so clients kept a corpse a step low on narrow stairs; one with gravity is sent standing on the floor.
int64_t ComputeSettledDeathZ(int64_t z, int64_t floorz, bool noGravity);

}} // namespace zx::netreplication

#endif // ZX_ACTORSTATE_COMPUTE_H
