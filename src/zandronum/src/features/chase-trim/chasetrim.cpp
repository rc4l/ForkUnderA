// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/chase-trim/chasetrim.h"

#include <cstdint>
#include <vector>

#include "actor.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "d_player.h"
#include "doomstat.h"
#include "g_level.h"
#include "m_random.h"
#include "features/net-session/netrole.h"
#include "p_local.h"
#include "p_enemy.h"
#include "features/monster-stairs/monsterstairs.h"

// [Doruk-Aksoy] Monsters this far (map units) from every player move every other chase call with double steps; 0 (default) turns it off.
CVAR(Int, sv_fua_farchase, 0, CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING)

// [Doruk-Aksoy] A monster whose re-plan found no direction still gets its missile check, as a walker does.
CVAR(Bool, sv_fua_stuckattack, true, CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING)

// [Doruk-Aksoy] Blocked-direction memo: 0 off, 1 on, 2 predicts but still tries every move and counts where it would have been wrong.
CVAR(Int, fua_dev_blockmemo, 1, CVAR_FUA_DEV_MODE)

// [Doruk-Aksoy] Re-plans in a row that must fail every direction before a walker counts as jammed and its memo, jam skip and wait apply.
CVAR(Int, sv_fua_jamafter, 3, CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING)

// [Doruk-Aksoy] Whether a walker with every direction known blocked skips its re-plan.
CVAR(Bool, fua_dev_jamskip, true, CVAR_FUA_DEV_MODE)

// [Doruk-Aksoy] Chase calls a jammed walker waits after each failed re-plan before moving again; 0 never waits.
CVAR(Int, sv_fua_jamwait, 2, CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING)

static FRandom pr_fuastuckwalk("FuaStuckWalk");

namespace zx { namespace chasetrim {

namespace {

// [Doruk-Aksoy] One jammed walker's memo, held off the actor: most actors never jam, so a pool entry is made at the first commit and given back when the walker moves.
struct MemoEntry
{
	BlockMemo memo;
	TObjPtr<AActor> blockers[kMemoSlots];
	AActor *owner;
};
std::vector<MemoEntry> g_memos;
std::vector<uint32_t> g_freeMemos;

// [Doruk-Aksoy] The walker's entry, or NULL; an index left over from another level's pool is never trusted.
MemoEntry *EntryOf(const AActor *actor)
{
	const uint32_t index = actor->FuaMemoIndex;
	if (index == 0 || index > g_memos.size())
		return NULL;
	MemoEntry &entry = g_memos[index - 1];
	return entry.owner == actor ? &entry : NULL;
}

// [Doruk-Aksoy] The walker's entry, made empty on first use.
MemoEntry &EntryFor(AActor *actor)
{
	if (MemoEntry *entry = EntryOf(actor))
		return *entry;
	uint32_t index;
	if (!g_freeMemos.empty())
	{
		index = g_freeMemos.back();
		g_freeMemos.pop_back();
	}
	else
	{
		g_memos.emplace_back();
		index = (uint32_t)g_memos.size();
	}
	MemoEntry &entry = g_memos[index - 1];
	entry.memo = BlockMemo();
	for (TObjPtr<AActor> &blocker : entry.blockers)
		blocker = NULL;
	entry.owner = actor;
	actor->FuaMemoIndex = index;
	return entry;
}

int g_stepScale = 1;
uint32_t g_worldEpoch = 0;	// [Doruk-Aksoy] Counts sector, line and polyobject changes that can open a wall.

struct Viewers
{
	int tic = -1;
	int count = 0;
	int64_t x[MAXPLAYERS];
	int64_t y[MAXPLAYERS];
};
Viewers g_viewers;

// [Doruk-Aksoy] The walker of the chase call under way and what its moves have shown so far.
struct Call
{
	AActor *actor = NULL;	// [Doruk-Aksoy] NULL outside a call, or once the walker moved.
	int64_t x = 0, y = 0, z = 0;
	int speed = -1;
	bool refreshed = false;	// [Doruk-Aksoy] Whether the memo's age and wall directions were read yet.
	bool jammed = false;	// [Doruk-Aksoy] Whether the walker is jammed, so its memo from earlier calls applies.
	uint8_t memoLines = 0;	// [Doruk-Aksoy] Wall directions the memo holds from here.
	uint8_t things = 0;		// [Doruk-Aksoy] Directions one thing alone refused in this call.
	uint8_t walls = 0;		// [Doruk-Aksoy] Directions a wall or ledge refused in this call.
	AActor *by[kMemoDirs] = {};
};
Call g_call;

enum Prediction { kNone, kThing, kWall };

struct MemoStats
{
	unsigned long long thingSkips, wallSkips, commits, full, predicted, wrongMoved, wrongOther, jamSkips, jamWaits;
};
MemoStats g_stats;
Prediction g_predicted = kNone;

const Viewers &CurrentViewers()
{
	if (g_viewers.tic != gametic)
	{
		g_viewers.tic = gametic;
		g_viewers.count = 0;
		for (int i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || players[i].mo == NULL)
				continue;
			const AActor *view = players[i].camera != NULL ? players[i].camera : players[i].mo;
			g_viewers.x[g_viewers.count] = view->x.Raw();
			g_viewers.y[g_viewers.count] = view->y.Raw();
			g_viewers.count++;
		}
	}
	return g_viewers;
}

bool WalkerRemembers(const AActor *actor)
{
	return fua_dev_blockmemo != 0 && !zx::net::InClientMode() && actor->player == NULL &&
		!(actor->flags & MF_SKULLFLY) && !(actor->flags6 & (MF6_TOUCHY | MF6_THRUSPECIES)) && !zx::monsterstairs::Active();
}

bool Rememberable(const AActor *blocker)
{
	return blocker->player == NULL && !(blocker->flags & MF_SPECIAL) && !(blocker->flags2 & MF2_PUSHABLE) &&
		!(blocker->flags4 & MF4_ACTLIKEBRIDGE) && !(blocker->flags6 & (MF6_TOUCHY | MF6_BUMPSPECIAL));
}

// [Doruk-Aksoy] The flags half of PIT_CheckThing: a solid blocker the walker cannot pass through.
bool Solid(const AActor *blocker, const AActor *walker)
{
	return blocker != walker && (blocker->flags & MF_SOLID) && !(blocker->flags & MF_NOCLIP) &&
		((walker->flags & MF_SOLID) || (walker->flags6 & MF6_BLOCKEDBYSOLIDACTORS)) &&
		!((blocker->flags2 | walker->flags2) & MF2_THRUACTORS);
}

Box BoxOf(const AActor *a)
{
	return Box{ a->x.Raw(), a->y.Raw(), a->z.Raw(), a->radius.Raw(), a->height.Raw() };
}

// [Doruk-Aksoy] The remembered actor that still refuses this step, else NULL; one gone or no longer solid is forgotten.
AActor *StillBlocking(AActor *actor, int dir, int64_t destX, int64_t destY)
{
	MemoEntry *entry = EntryOf(actor);
	if (entry == NULL)
		return NULL;
	BlockMemo &memo = entry->memo;
	const int slot = ComputeMemoSlot(memo, dir);
	if (slot < 0)
		return NULL;
	AActor *blocker = entry->blockers[slot];
	if (blocker == NULL || !Rememberable(blocker) || !Solid(blocker, actor))
	{
		ComputeMemoForgetSlot(memo, slot);
		return NULL;
	}
	// [Doruk-Aksoy] P_CheckPosition lets floaters step onto nothing, and actors without PASSMOBJ meet at any height.
	const int64_t maxStep = (actor->flags & MF_FLOAT) ? INT64_MIN / 2 : actor->MaxStepHeight.Raw();
	const bool tall = (i_compatflags & COMPATF_NO_PASSMOBJ) || !(actor->flags2 & MF2_PASSMOBJ);
	return ComputeStillBlocks(BoxOf(actor), maxStep, destX, destY, BoxOf(blocker), tall) ? blocker : NULL;
}

// [Doruk-Aksoy] Reads the memo's age and wall directions once a call; a walker with no entry remembers nothing.
void Refresh(AActor *actor)
{
	if (!g_call.refreshed)
	{
		g_call.refreshed = true;
		MemoEntry *entry = EntryOf(actor);
		if (entry != NULL && ComputeJammed(actor->FuaJamCount, sv_fua_jamafter) && ComputeMemoRefresh(entry->memo, level.maptime, kMemoMaxAge))
		{
			g_call.jammed = true;
			g_call.memoLines = ComputeMemoLines(entry->memo, g_call.x, g_call.y, g_call.z, g_worldEpoch);
		}
	}
}

// [Doruk-Aksoy] Whether this direction is known blocked for a step of speed: refused earlier in this call, or by a remembered wall or actor.
bool KnownBlocked(AActor *actor, int dir, int speed, AActor *&blocker)
{
	const unsigned bit = 1u << dir;
	blocker = NULL;
	Refresh(actor);
	if ((g_call.walls | g_call.memoLines) & bit)
		return true;
	if (g_call.things & bit)
	{
		blocker = g_call.by[dir];
		return true;
	}
	const MemoEntry *entry = g_call.jammed ? EntryOf(actor) : NULL;
	if (entry == NULL || entry->memo.dirSlots == 0)
		return false;
	const int64_t dx = FixedMul(speed, xspeed[dir]).Raw(), dy = FixedMul(speed, yspeed[dir]).Raw();
	const int steps = ComputeMoveSteps(dx, dy, actor->radius.Raw());
	blocker = StillBlocking(actor, dir, g_call.x + dx / steps, g_call.y + dy / steps);
	return blocker != NULL;
}

// [Doruk-Aksoy] A walker that stayed where it was keeps this call's blocks for its next call.
void Commit(AActor *actor)
{
	if ((g_call.things | g_call.walls) == 0 || actor->x.Raw() != g_call.x || actor->y.Raw() != g_call.y || actor->z.Raw() != g_call.z)
		return;
	g_stats.commits++;
	MemoEntry &entry = EntryFor(actor);
	BlockMemo &memo = entry.memo;
	if (g_call.walls != 0)
		ComputeMemoRecordLines(memo, g_call.walls, g_call.x, g_call.y, g_call.z, g_worldEpoch, level.maptime);
	for (int dir = 0; dir < kMemoDirs; dir++)
	{
		AActor *blocker = g_call.by[dir];
		if (!(g_call.things & (1u << dir)))
			continue;
		int same = -1;
		for (int s = 0; s < kMemoSlots; s++)
		{
			if (entry.blockers[s] == blocker)
			{
				for (int d = 0; d < kMemoDirs; d++)
					same = ComputeMemoSlot(memo, d) == s ? s : same;
			}
		}
		const int slot = ComputeMemoRecordActor(memo, dir, same, level.maptime);
		if (slot < 0)
			g_stats.full++;
		else
			entry.blockers[slot] = blocker;
	}
}

}

FarStep ChaseStep(AActor *actor)
{
	const Viewers &v = CurrentViewers();
	const bool far = ComputeFarFromViewers(actor->x.Raw(), actor->y.Raw(), v.x, v.y, v.count, (int64_t)sv_fua_farchase << 16);
	FarStep step = ComputeFarChaseStep(far, actor->FuaChasePhase);
	int movecount = actor->movecount;
	ComputeFarKeepCount(step, movecount);
	actor->movecount = (SWORD)movecount;
	return step;
}

int StepScale()
{
	return g_stepScale;
}

ChaseMove::ChaseMove(AActor *actor, int scale) : savedScale(g_stepScale)
{
	g_stepScale = scale;
	g_call = Call();
	if (!WalkerRemembers(actor))
		return;
	g_call.actor = actor;
	g_call.x = actor->x.Raw();
	g_call.y = actor->y.Raw();
	g_call.z = actor->z.Raw();
}

ChaseMove::~ChaseMove()
{
	if (g_call.actor != NULL)
		Commit(g_call.actor);
	g_call = Call();
	g_stepScale = savedScale;
}

void OnReplanFailed(AActor *actor)
{
	if (zx::net::InClientMode())
		return;
	if (sv_fua_stuckattack)
		actor->movecount = ComputeStuckMoveCount(actor->movecount, pr_fuastuckwalk());
	ComputeJamCount(actor->FuaJamCount, true);
	ComputeFarHold(actor->FuaChasePhase, kFarHoldCalls);
	if (ComputeJammed(actor->FuaJamCount, sv_fua_jamafter))
		actor->FuaJamWait = (uint8_t)(sv_fua_jamwait < 0 ? 0 : sv_fua_jamwait > 255 ? 255 : (int)sv_fua_jamwait);
}

void WorldChanged()
{
	g_worldEpoch++;
}

bool JamWaits(AActor *actor)
{
	if (g_call.actor != actor || !ComputeJammed(actor->FuaJamCount, sv_fua_jamafter) || !ComputeJamWait(actor->FuaJamWait))
		return false;
	g_stats.jamWaits++;
	return true;
}

bool SkipJammedReplan(AActor *actor)
{
	// [Doruk-Aksoy] Left to P_NewChaseDir: strafing away from melee, stepping back from a ledge, and sludge-scaled steps.
	if (!fua_dev_jamskip || g_call.actor != actor || actor->target == NULL || !ComputeJammed(actor->FuaJamCount, sv_fua_jamafter) ||
		(actor->flags3 & MF3_AVOIDMELEE) || (level.flags3 & LEVEL3_AVOIDMELEE) || (compatflags & COMPATF_MBFMONSTERMOVE) ||
		(actor->floorz - actor->dropoffz > actor->MaxDropOffHeight && !(actor->flags & (MF_DROPOFF | MF_FLOAT))))
		return false;
	const int speed = (int)(actor->Speed) * g_stepScale;
	if (g_call.speed >= 0 && g_call.speed != speed)
		return false;
	for (int dir = 0; dir < kMemoDirs; dir++)
	{
		AActor *blocker;
		if (!KnownBlocked(actor, dir, speed, blocker))
			return false;
	}
	g_stats.jamSkips++;
	actor->strafecount = 0;
	OnReplanFailed(actor);
	actor->movedir = DI_NODIR;
	return true;
}

bool FailKnownBlocked(AActor *actor, int speed)
{
	g_predicted = kNone;
	if (g_call.actor != actor)
		return false;
	if (g_call.speed < 0)
		g_call.speed = speed;
	else if (g_call.speed != speed)
		return false;
	AActor *blocker = NULL;
	if (KnownBlocked(actor, actor->movedir, speed, blocker))
		g_predicted = kWall;
	if (blocker != NULL)
		g_predicted = kThing;
	if (g_predicted == kNone)
		return false;
	g_stats.predicted++;
	if (fua_dev_blockmemo == 2)
		return false;
	if (g_stepScale > 1)
		ComputeFarHold(actor->FuaChasePhase, kFarHoldCalls);
	// [Doruk-Aksoy] What a refused move leaves behind; a wall's own line is not remembered.
	if (g_predicted == kThing)
	{
		g_stats.thingSkips++;
		actor->BlockingMobj = blocker;
		actor->BlockingLine = NULL;
	}
	else
	{
		g_stats.wallSkips++;
		actor->BlockingMobj = NULL;
	}
	spechit.Clear();
	g_predicted = kNone;
	return true;
}

void AfterMove(AActor *actor, int speed, bool moved, AActor *hardBlocker, bool wall, bool stayed)
{
	if (g_predicted != kNone)
	{
		if (moved) g_stats.wrongMoved++;
		else if (g_predicted == kThing ? hardBlocker == NULL : !wall) g_stats.wrongOther++;
		g_predicted = kNone;
	}
	// [Doruk-Aksoy] A refused double step means tight quarters: walk single steps every call for a while.
	if (!moved && g_stepScale > 1)
		ComputeFarHold(actor->FuaChasePhase, kFarHoldCalls);
	if (moved)
		ComputeJamCount(actor->FuaJamCount, false);
	if (g_call.actor != actor || g_call.speed != speed)
		return;
	// [Doruk-Aksoy] A split step can move the walker and still fail, which leaves every wall it knew behind.
	if (moved || !stayed)
	{
		ForgetMemo(actor);
		g_call.actor = NULL;
		return;
	}
	const unsigned bit = 1u << actor->movedir;
	if (hardBlocker != NULL && Rememberable(hardBlocker))
	{
		g_call.things |= (uint8_t)bit;
		g_call.by[actor->movedir] = hardBlocker;
	}
	else if (wall)
		g_call.walls |= (uint8_t)bit;
}

void ForgetMemo(AActor *actor)
{
	MemoEntry *entry = EntryOf(actor);
	if (entry == NULL)
		return;
	entry->owner = NULL;
	for (TObjPtr<AActor> &blocker : entry->blockers)
		blocker = NULL;
	g_freeMemos.push_back(actor->FuaMemoIndex);
	actor->FuaMemoIndex = 0;
}

size_t SubstituteMemo(AActor *actor, DObject *old, DObject *notOld)
{
	MemoEntry *entry = EntryOf(actor);
	if (entry == NULL)
		return 0;
	size_t changed = 0;
	for (TObjPtr<AActor> &blocker : entry->blockers)
	{
		if ((DObject *)(AActor *)blocker == old)
		{
			blocker = static_cast<AActor *>(notOld);
			changed++;
		}
	}
	return changed;
}

void MarkMemo(AActor *actor)
{
	MemoEntry *entry = EntryOf(actor);
	if (entry == NULL)
		return;
	for (TObjPtr<AActor> &blocker : entry->blockers)
		GC::Mark(blocker);
}

} } // namespace zx::chasetrim

// [Doruk-Aksoy] Prints the blocked-direction memo's counts since the last reset; any argument resets them.
DEV_MODE_CCMD(fua_dev_blockmemo_report)
{
	const zx::chasetrim::MemoStats &s = zx::chasetrim::g_stats;
	Printf("blockmemo thingSkips %llu wallSkips %llu commits %llu full %llu predicted %llu wrongMoved %llu wrongOther %llu jamSkips %llu jamWaits %llu\n",
		s.thingSkips, s.wallSkips, s.commits, s.full, s.predicted, s.wrongMoved, s.wrongOther, s.jamSkips, s.jamWaits);
	if (argv.argc() > 1)
		zx::chasetrim::g_stats = zx::chasetrim::MemoStats();
}
