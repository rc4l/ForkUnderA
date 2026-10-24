// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a server sends its clients of an actor that took damage.

#ifndef ZX_ACTORSTATE_COMPUTE_H
#define ZX_ACTORSTATE_COMPUTE_H

#include <vector>

namespace zx { namespace netreplication {

// [rc4l] Whether a damaged actor's health goes to every client: a non-player still alive, since a player's goes with its own damage command and a death with the death.
bool ComputeSendsWoundedHealth(bool isPlayer, int health);

// [rc4l] Add a wounded actor's network id to the tic's pending list once, so a shotgun's seven pellets send one health; false for no id (a server-only actor) or one already pending.
bool ComputeAddPendingHealth(std::vector<int> &pending, int netId);

}} // namespace zx::netreplication

#endif // ZX_ACTORSTATE_COMPUTE_H
