// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/aimassist/aimassist.h"
#include "features/aimassist/computation/aimassist_compute.h"

#include "actor.h"
#include "c_cvars.h"
#include "d_player.h"
#include "d_ticcmd.h"
#include "doomdef.h"
#include "doomstat.h"
#include "features/net-session/netrole.h"
#include "p_local.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// [Doruk-Aksoy] Turn the local player's view onto the nearest monster in sight each tic, single player only (debug).
CVAR(Bool, fua_debug_aimassist, false, 0)
// [Doruk-Aksoy] The heading in degrees move input is taken along while the assist turns the view (debug).
CVAR(Float, fua_debug_aimassist_heading, 0.f, 0)
// [Doruk-Aksoy] How far away in map units a monster can be picked (debug).
CVAR(Float, fua_debug_aimassist_range, 1500.f, 0)

namespace zx { namespace aimassist {

namespace
{
// [Doruk-Aksoy] Sight checks a tic at most when a new target is picked, nearest-ahead first.
constexpr size_t kSightTries = 24;

// [Doruk-Aksoy] The held target's network id, 0 for none.
unsigned short g_target = 0;

bool Aimable(const AActor *mo)
{
	return mo != NULL && mo->player == NULL && mo->health > 0 && (mo->flags3 & MF3_ISMONSTER) && (mo->flags & MF_SHOOTABLE) && !(mo->flags & MF_CORPSE);
}

double Dist(const AActor *from, const AActor *to)
{
	return std::hypot(FIXED2DBL(to->x - from->x), FIXED2DBL(to->y - from->y));
}

double ViewDeg(const AActor *mo)
{
	return (double)mo->angle * (360.0 / 4294967296.0);
}

// [Doruk-Aksoy] The held target while it lives in range and sight, else the best-scored monster in sight.
AActor *Pick(AActor *pmo, double range)
{
	AActor *held = g_target ? g_ActorNetIDList.findPointerByID(g_target) : NULL;
	if (Aimable(held) && Dist(pmo, held) <= range && P_CheckSight(pmo, held, 0)) return held;
	std::vector<AActor *> found;
	std::vector<double> scores;
	TThinkerIterator<AActor> it;
	for (AActor *mo; (mo = it.Next()) != NULL;)
	{
		if (!Aimable(mo)) continue;
		found.push_back(mo);
		scores.push_back(ComputeTargetScore(Dist(pmo, mo), ComputeOffDeg(ViewDeg(pmo), FIXED2DBL(mo->x - pmo->x), FIXED2DBL(mo->y - pmo->y)), range));
	}
	const std::vector<size_t> order = ComputeTargetOrder(scores);
	g_target = 0;
	for (size_t k = 0; k < order.size() && k < kSightTries; k++)
	{
		AActor *mo = found[order[k]];
		if (P_CheckSight(pmo, mo, 0)) { g_target = mo->NetID; return mo; }
	}
	return NULL;
}
}

void Apply(ticcmd_t *cmd, int playernum)
{
	if (!fua_debug_aimassist || demoplayback || gamestate != GS_LEVEL || !zx::net::IsSinglePlayer()) return;
	AActor *pmo = players[playernum].mo;
	if (pmo == NULL) return;
	const double heading = fua_debug_aimassist_heading;
	const AActor *target = Pick(pmo, fua_debug_aimassist_range);
	const double eyeZ = FIXED2DBL(pmo->z + pmo->height / 2) + (pmo->IsKindOf(RUNTIME_CLASS(APlayerPawn)) ? FIXED2DBL(static_cast<APlayerPawn *>(pmo)->AttackZOffset) : 8.0);
	const Aim want = target != NULL
		? ComputeAimAt(FIXED2DBL(pmo->x), FIXED2DBL(pmo->y), eyeZ, FIXED2DBL(target->x), FIXED2DBL(target->y), FIXED2DBL(target->z + target->height / 2))
		: Aim{ heading, 0.0 };
	// [Doruk-Aksoy] pitch is a widened fixed_t, but always within a quarter turn, so it fits the 32-bit angle.
	const Turn turn = ComputeTicTurn(pmo->angle, (int32_t)pmo->pitch, ComputeDegToAngle(want.yawDeg), ComputePitchDegToAngle(want.pitchDeg));
	const Move move = ComputeMoveOnHeading(cmd->ucmd.forwardmove, cmd->ucmd.sidemove, heading, want.yawDeg);
	cmd->ucmd.yaw = turn.yaw;
	cmd->ucmd.pitch = turn.pitch;
	cmd->ucmd.forwardmove = move.forward;
	cmd->ucmd.sidemove = move.side;
}

}} // namespace zx::aimassist
