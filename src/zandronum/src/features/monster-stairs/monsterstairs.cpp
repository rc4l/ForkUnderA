// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/monster-stairs/monsterstairs.h"

#include "actor.h"
#include "c_cvars.h"
#include "g_level.h"
#include "gi.h"
#include "p_local.h"
#include "features/monster-stairs/computation/monsterstairs_compute.h"
#include "features/net-session/syncedcvars.h"

// [Doruk-Aksoy] The server's default for maps and mods that do not choose; clients predict monster walking, so they are sent it.
CUSTOM_CVAR(Bool, sv_fua_monsterstairs, false, CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING)
{
	zx::net::SendSyncedCVar(self);
}

static zx::net::SyncedCVar SyncMonsterStairs(sv_fua_monsterstairs);

namespace zx { namespace monsterstairs {

bool Active()
{
	const Setting map = level.info != NULL ? level.info->fua_monsterStairs : Setting::Unset;
	return ComputeMonsterStairsEnabled(map, gameinfo.fua_monsterStairs, sv_fua_monsterstairs);
}

bool LiftOntoStairs(AActor *actor)
{
	// [Doruk-Aksoy] A spawn or teleport reads only the floor at its centre, so a walker wider than the steps can sit below the ones its box covers, where every move is too big a step up.
	if ((actor->flags & (MF_NOGRAVITY | MF_FLOAT | MF_NOCLIP)) || (actor->flags2 & MF2_ONMOBJ) || actor->z > actor->floorz || !Active())
		return false;
	FCheckPosition tm;
	if (!P_CheckPosition(actor, actor->x, actor->y, tm) ||
		!ComputeStairLift(actor->z.Raw(), tm.floorz.Raw(), tm.maxlinedrop.Raw(), actor->MaxStepHeight.Raw()))
		return false;
	const fixed_t savedz = actor->z;
	actor->z = tm.floorz;
	if (!P_TestMobjZ(actor))
	{
		actor->z = savedz;
		return false;
	}
	actor->floorz = tm.floorz;
	actor->dropoffz = tm.dropoffz;
	actor->floorsector = tm.floorsector;
	actor->floorpic = tm.floorpic;
	return true;
}

void ResetGameInfo()
{
	gameinfo.fua_monsterStairs = Setting::Unset;
}

}} // namespace zx::monsterstairs
