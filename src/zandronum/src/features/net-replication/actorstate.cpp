// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-replication/actorstate.h"

#include "actor.h"
#include "sv_commands.h"

#include "features/net-replication/computation/actorstate_compute.h"
#include "features/net-session/netrole.h"

#include <vector>

namespace {
std::vector<int> g_wounded;
}

namespace zx { namespace netreplication {

void NoteDamaged(AActor *target)
{
	if (target == NULL || !zx::net::IsServer()) return;
	if (ComputeSendsWoundedHealth(target->player != NULL, target->health)) ComputeAddPendingHealth(g_wounded, target->NetID);
}

void SendWoundedHealth()
{
	for (int netId : g_wounded)
	{
		AActor *actor = g_ActorNetIDList.findPointerByID(netId);
		// [rc4l] One that died later in the tic sent its death instead.
		if (actor != NULL && ComputeSendsWoundedHealth(actor->player != NULL, actor->health)) SERVERCOMMANDS_SetThingHealth(actor);
	}
	g_wounded.clear();
}

}} // namespace zx::netreplication
