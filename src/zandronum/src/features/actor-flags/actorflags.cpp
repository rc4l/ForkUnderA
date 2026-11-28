// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/actor-flags/actorflags.h"

#include "actor.h"
#include "c_console.h"
#include "doomdata.h"
#include "g_level.h"
#include "invasion.h"
#include "network.h"
#include "thingdef/thingdef.h"
#include "features/actor-flags/computation/actorflags_compute.h"
#include "features/net-replication/actorstate.h"

namespace zx { namespace actorflags {

namespace {

// [Doruk-Aksoy] Which of the actor's flag words `word` is, as the flag send names it.
// [rc4l] uzdoom@ca012bc9b typed the flag words, so they compare as addresses.
FlagSet FlagSetOf(AActor *a, const void *word)
{
	const struct { const void *word; FlagSet set; } words[] = {
		{ &a->flags, FLAGSET_FLAGS }, { &a->flags2, FLAGSET_FLAGS2 }, { &a->flags3, FLAGSET_FLAGS3 }, { &a->flags4, FLAGSET_FLAGS4 },
		{ &a->flags5, FLAGSET_FLAGS5 }, { &a->flags6, FLAGSET_FLAGS6 }, { &a->flags7, FLAGSET_FLAGS7 },
		// [MGOOOOOO] flags8 was never wired up here, so A_ChangeFlag on an MBF21 flag silently desynced clients; flags9 is ZandroX's own word.
		{ &a->flags8, FLAGSET_FLAGS8 }, { &a->flags9, FLAGSET_FLAGS9 },
		// [rc4l] Movement-model flags (see actor.h MV_*).
		{ &a->mvFlags, FLAGSET_MVFLAGS }, { &a->fua_flags, FLAGSET_FUAFLAGS }, { &a->renderflags, FLAGSET_RENDERFLAGS },
		{ &a->BounceFlags, FLAGSET_BOUNCEFLAGS }, { &a->renderflags2, FLAGSET_RENDERFLAGS2 }, { &a->STFlags, FLAGSET_FLAGSST },
	};
	for (const auto &w : words)
		if (w.word == word)
			return w.set;
	return FLAGSET_UNKNOWN;
}

} // namespace

bool ChangeActorFlag(AActor *actor, const char *name, bool on, bool printError, bool localOnly)
{
	if (actor == NULL)
		return false;
	const PClass *cls = actor->GetClass();
	const FlagName parts = ComputeFlagName(name);
	FFlagDef *fd = FindFlag(cls, parts.part1.c_str(), parts.dotted ? parts.part2.c_str() : NULL);
	if (fd == NULL)
	{
		if (printError)
			Printf("Unknown flag '%s' in '%s'\n", name, cls->TypeName.GetChars());
		return false;
	}

	const bool killBefore = ComputeCountsKill(actor->CountsAsKill(), actor->health);
	const bool itemBefore = (actor->flags & MF_COUNTITEM) != 0;
	const bool secretBefore = (actor->flags5 & MF5_COUNTSECRET) != 0;

	if (fd->structoffset == -1)
		HandleDeprecatedFlags(actor, cls->ActorInfo, on, fd->flagbit);
	else
	{
		// [BB] The server handles the flag change.
		// [rc4l] Ported from zandronum@873abcdc7cf96f2e1daffa583896faed4afd6bc7: a CLIENTSIDEONLY actor changes its own flags.
		if (!localOnly && NETWORK_InClientModeAndActorNotClientHandled(actor))
			return true;

		ActorFlags *flagp = (ActorFlags *)(((char *)actor) + fd->structoffset);
		// [EP] Store the old value in order to save bandwidth.
		const DWORD oldflag = flagp->GetValue();
		// If these 2 flags get changed we need to update the blockmap and sector links.
		const bool linkchange = flagp == &actor->flags && (fd->flagbit == MF_NOBLOCKMAP || fd->flagbit == MF_NOSECTOR);
		if (linkchange) actor->UnlinkFromWorld();
		ModActorFlag(actor, fd, on);
		if (linkchange) actor->LinkToWorld();
		// [BB] Let the clients know about the flag change.
		if (!localOnly && flagp->GetValue() != oldflag)
			netreplication::SendThingFlags(actor, FlagSetOf(actor, flagp));
	}

	// [Doruk-Aksoy] The map's totals are the server's, so a clientside script's change leaves them alone.
	if (localOnly)
		return true;
	const int kills = ComputeCountDelta(killBefore, ComputeCountsKill(actor->CountsAsKill(), actor->health));
	const int items = ComputeCountDelta(itemBefore, (actor->flags & MF_COUNTITEM) != 0);
	const int secrets = ComputeCountDelta(secretBefore, (actor->flags5 & MF5_COUNTSECRET) != 0);
	level.total_monsters += kills;
	level.total_items += items;
	level.total_secrets += secrets;
	// [AK] Update the invasion monster count accordingly.
	if (kills != 0)
		INVASION_UpdateMonsterCount(actor, kills < 0);
	netreplication::SendMapTotals(kills != 0, items != 0, secrets != 0);
	return true;
}

}} // namespace zx::actorflags
