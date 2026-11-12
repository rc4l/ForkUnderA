// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-replication/actorstate.h"

#include "actor.h"
#include "d_netinf.h"
#include "d_player.h"
#include "network.h"
#include "p_local.h"
#include "sv_commands.h"
#include "sv_main.h"

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

void SendFloorClip(AActor *actor)
{
	if (actor != NULL && zx::net::IsServer()) SERVERCOMMANDS_SetThingFloorClip(actor);
}

void SendThingProperty(AActor *actor, int property)
{
	if (actor != NULL && zx::net::IsServer()) SERVERCOMMANDS_SetThingProperty(actor, property);
}

// [rc4l] Ported from zandronum@7d12f80d364050901f782f5e013f780956202611: the send P_SpawnPuff made, on its own so a rail sends its puff once the puff survived the sky.
void SendPuff(AActor *puff, AActor *source, unsigned state, int flags)
{
	// [rc4l] Theirs also returns for no source, which sent such a puff to no client at all.
	if (puff == NULL || !zx::net::IsServer()) return;
	const bool obituary = puff->GetClass()->Meta.GetMetaString(AMETA_Obituary) != NULL && (flags & PF_TEMPORARY);
	const int send = ComputePuffSend(state == STATE_SPAWN, (flags & PF_HITTHING) && puff->SeeSound != 0, puff->AttackSound != 0, obituary);
	if (send == kPuffNoNetID)
	{
		SERVERCOMMANDS_SpawnPuffNoNetID(puff, state, false);
		return;
	}
	if (send == kPuffWithNetID)
	{
		// [rc4l] Its sounds play by its net id.
		if (puff->NetID == 0)
		{
			puff->NetID = g_ActorNetIDList.getNewID();
			g_ActorNetIDList.useID(puff->NetID, puff);
		}
		SERVERCOMMANDS_SpawnPuff(puff);
		return;
	}
	// [CK] It is always sent when fired from a non-player.
	if (source == NULL || source->player == NULL)
	{
		SERVERCOMMANDS_SpawnPuff(puff);
		return;
	}
	const unsigned shooter = static_cast<unsigned>(source->player - players);
	const bool clientSide = (source->player->userinfo.GetClientFlags() & CLIENTFLAGS_CLIENTSIDEPUFFS) != 0;
	for (unsigned player = 0; player < MAXPLAYERS; player++)
	{
		if (!SERVER_IsValidClient(player)) continue;
		if (ComputeSkipsPuffFor(player == shooter, (puff->NetworkFlags & NETFL_NONETID) != 0, clientSide)) continue;
		// [BB] If the puff has a net ID, it must be spawned with one.
		SERVERCOMMANDS_SpawnPuff(puff, player, SVCF_ONLYTHISCLIENT);
	}
}

}} // namespace zx::netreplication
