//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: ghostactor.cpp
//
// Description: The render shim for the snapshot ghost table: mirror
// actors, local animation, smoothing, the render cull, near-tier collision,
// hiding the legacy copies, cosmetic event copies, mirror spawns, the client
// report and the fua_csnap_ghostinfo command. See ghostactor.h.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <math.h>


#include "doomdef.h"
#include "doomstat.h"
#include "actor.h"
#include "r_data/renderstyle.h"
#include "r_data/sprites.h"	// sprite names for fua_csnap_ghostinfo
#include "r_state.h"
#include "d_player.h"
#include "p_local.h"
#include "statnums.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "c_console.h"
#include "i_system.h"
#include "r_utility.h"	// [Kaori-Chani] r_NoInterpolate, which forces the frac to 1
#include "i_tictime.h"
#include "network.h"
#include "cl_main.h"		// CLIENT_MoveThing, for the real player actors
#include "tables.h"
#include "tarray.h"
#include "zstring.h"
#include "s_sound.h"	// the respawn sound a mirrored item makes at its ghost
#include "a_pickups.h"	// AInventory::DoRespawn, for the same
#include "features/actormaterials/actormaterials.h"	// a mirror's material
#include "features/net-snapshot/format.h"
#include "features/net-snapshot/entities.h"
#include "features/net-snapshot/apply.h"
#include "features/net-snapshot/ghostactor.h"
#include "features/net-snapshot/movers.h"
#include "features/net-snapshot/polyobj.h"
#include "features/net-snapshot/computation/anim_compute.h"
#include "features/net-snapshot/computation/mover_compute.h"
#include "features/net-snapshot/computation/smooth_compute.h"
#include "features/net-snapshot/computation/replay_compute.h"	// [Kaori-Chani] ComputeCensusAdd
#include "features/net-snapshot/computation/wall_compute.h"
#include "features/net-snapshot/capture.h"
#include "features/net-snapshot/record.h"
#include "features/net-snapshot/selftest.h"
#include "networkshared.h"	// BYTESTREAM_s, for SVC2_SNAPCOSMETIC
#include "r_data/r_translate.h"	// TRANSLATION, for SVC2_SNAPCOSMETIC

using namespace zx::netsnapshot;

// Master enable for ghost mirroring; GhostsFromNetwork turns it on.
CVAR(Bool, fua_csnap_ghosts, false, 0)

// Presentation settings the mirror applies on this client's behalf.
EXTERN_CVAR(Int, cl_rockettrails)
EXTERN_CVAR(Bool, fua_csnap_nolocalsound)
EXTERN_CVAR(Int, fua_csnap_missile_extrap)
EXTERN_CVAR(Bool, nomonsterinterpolation)
EXTERN_CVAR(Bool, cl_capfps)

// Set by the network path when live snapshot traffic is arriving, so
// ghosts appear without the player having to enable anything.
void zx::netsnapshot::GhostsFromNetwork(bool active)
{
	if (active && (fua_csnap_ghosts == false))
		fua_csnap_ghosts = true;
	else if ((active == false) && fua_csnap_ghosts)
	{
		fua_csnap_ghosts = false;
		GhostClearAll();
	}
}

//*****************************************************************************
//	MIRROR ACTOR TABLE

struct GhostShadow
{
	FState * state;
	int		tics;
	bool	valid;
};

static GhostShadow * g_shadow = NULL;

// The state index the wire last reported, so the census scores once per
// server animation change.
static WORD * g_shadowLastState = NULL;

// The drawn sprite and frame last written to the .csnap, so CSNAP_MIRROR
// fires only on a change.
static DWORD * g_recLastDrawn = NULL;

// The gametic each ghost was last scored, bucketing a score by its own gap.
static int* g_shadowLastTic = NULL;

static TObjPtr<AActor>* g_mirror = NULL;

// An entity's properties as the server last sent them, by NetID: held rather than applied
// and forgotten, so a mirror made later is built with them.
static TMap<unsigned int, SnapProps> g_ghostProps;
static unsigned int g_propsToMirror = 0, g_propsHeld = 0;

// Scale, roll and material onto one mirror. A flag the set leaves out is the class
// default, which a mirror spawned from that class already has.
static void ghost_ApplyPropsTo(AActor * pMirror, const SnapProps & props)
{
	if (pMirror == NULL)
		return;
	const AActor * pDef = pMirror->GetDefault();
	pMirror->scaleX = (props.flags & SNAP_PROPS_SCALE)
		? DequantScale(props.scaleX) : pDef->scaleX;
	pMirror->scaleY = (props.flags & SNAP_PROPS_SCALE)
		? DequantScale(props.scaleY) : pDef->scaleY;
	pMirror->roll = (props.flags & SNAP_PROPS_ROLL)
		? (((angle_t)props.roll) << 16) : pDef->roll;
	if (props.flags & SNAP_PROPS_MATERIAL)
	{
		char name[SNAP_PROPS_MATNAME_MAX + 1];
		memcpy(name, props.material, props.matLen);
		name[props.matLen] = '\0';
		zx::actormaterials::ApplyActorMaterial(pMirror, name);
	}
	else
		zx::actormaterials::ApplyActorMaterial(pMirror,
			zx::actormaterials::MaterialName(pDef->fua_material - 1));

	// After the material, which is what decides whether a uniform index exists at
	// all; kind 4, as the joiner path sends an actor's own rows.
	if (props.flags & SNAP_PROPS_UNIFORMS)
	{
		for (unsigned int u = 0; u < SNAP_PROPS_UNIFORM_SLOTS; ++u)
		{
			if ((props.uniSet & (1u << u)) == 0)
				continue;
			const zx::acsshaders::UniformValue value = { 4, { props.uni[u][0],
				props.uni[u][1], props.uni[u][2], props.uni[u][3] } };
			zx::actormaterials::ApplyActorUniform(pMirror, (int)u, value);
		}
	}
}

// From the event: kept for this id, and onto its mirror where one stands.
static void ghost_ApplyProps(unsigned int id, const SnapProps & props)
{
	g_ghostProps[id] = props;
	if ((g_mirror != NULL) && (id < SNAP_MAXID) && (g_mirror[id] != NULL))
	{
		ghost_ApplyPropsTo(g_mirror[id], props);
		++g_propsToMirror;
	}
	else
		++g_propsHeld;
}

// Every property event this client has been told about, for the census line.
unsigned int zx::netsnapshot::PropsKept()
{
	return g_ghostProps.CountUsed();
}

void zx::netsnapshot::PropsStats(unsigned int & toMirror, unsigned int & held, bool reset)
{
	toMirror = g_propsToMirror;
	held = g_propsHeld;
	if (reset)
		g_propsToMirror = g_propsHeld = 0;
}

static BYTE* g_hadMirror = NULL;		// diag: id had a live mirror last frame

// The ids with a mirror, so the sweep and the teardown cost the population.
static TArray<unsigned int> g_mirrorIds;

// The wire state index each mirror was last synced to, so a resync waits for
// the server's animation to change.
static WORD* g_mirrorSyncedState = NULL;

// The wire frame the hard resync last corrected each id to.
static WORD * g_mirrorHardFrame = NULL;

// The wall clamp's trace cached per mirror on its path, `stop` in map units
// along it (negative: no wall within reach).
struct GhostWallTrace
{
	fixed_t	ox, oy, oz, vx, vy, vz;
	float	accelZ;
	WORD	capMS;
	float	stop;
};
static GhostWallTrace * g_wallTrace = NULL;
static unsigned int g_wallTraces = 0, g_wallClamped = 0, g_ceilClamped = 0;

static SnapSmooth * g_smooth = NULL;
static int * g_slideTic = NULL;		// the game tic each mirror last moved for, for the slide
static int g_slideFrameTic = -1;	// the game tic the slides were last ended for
static unsigned int g_smoothEpoch = 0;
static bool g_smoothInReset = false;

// Threshold in units per tic of the record gap (up to 4) for stepped ghosts:
// over p99 running (21) with room for strafe-running and knockback; a teleport
// or respawn still snaps. 0 off.
CVAR(Int, fua_csnap_smooth_step, 32, CVAR_NOSETBYACS)
// The units of a correction to a turning or unproven missile outside the
// record's rounding cell that are smoothed, past which it lands (0 off).
CVAR(Int, fua_csnap_smooth_nonballistic, 1, CVAR_NOSETBYACS)
// The same threshold for an extrapolated ballistic missile, 0, as easing its
// small corrections only cost it accuracy (design doc, Error smoothing).
CVAR(Int, fua_csnap_smooth_ballistic, 0, CVAR_NOSETBYACS)
// Stepped ghosts smoothed: 0 none, 1 remote players, 2 all. 2 relinks a moving
// monster every frame instead of once per record; measured before default.
CVAR(Int, fua_csnap_smooth_entities, 1, CVAR_NOSETBYACS)

// The smoothing census, window totals (SnapSmoothStats).
static SnapSmoothStats g_sm;

// [Kaori-Chani] The census describes a tic, taken from the last refill in it: a packet's
// fold is held until the tic turns over, so a second packet's replaces the first's.
static SnapSmoothStats g_smTic;
static int g_smTicCounted = -1;

// The last event sequence applied to each mirror, as an event changes no frame.
static BYTE * g_mirrorEventSeq = NULL;

// The gametic each mirror's animation last advanced to, low 16 bits.
static WORD* g_mirrorAnimTic = NULL;

// The server tic of each id's previous APPEAR record, for the gap census.
static int * g_stLastAppearTic = NULL;

// The client-side render cull's tiers by distance, with hysteresis
// (cl_ghostactor.md, A frame).

// Records drawn-state changes to the .csnap for one id in N, by id modulo
// (0 records none, 1 all).
CVAR(Int, fua_csnap_recmirror, 0, CVAR_NOSETBYACS)

// Near-tier collision, mirrors inside fua_csnap_collide_dist of the
// local player blocking movement.
CVAR(Bool, fua_csnap_collide, true, CVAR_NOSETBYACS|CVAR_ARCHIVE)

// The collision radius in map units, covering the local player's reach within
// a prediction replay.
CVAR(Int, fua_csnap_collide_dist, 512, CVAR_NOSETBYACS| CVAR_ARCHIVE)

CVAR(Bool, fua_csnap_rendercull, true, CVAR_ARCHIVE)
CVAR(Int, fua_csnap_cull_near, 3000, CVAR_ARCHIVE)	// full work within this radius
CVAR(Int, fua_csnap_cull_far, 6000, CVAR_ARCHIVE)	// skip-most beyond this radius

// A hidden legacy actor also leaves the sector lists the renderer walks.

// Hysteresis band: a ghost must cross a threshold by this much to change tier,
// so one sitting exactly on a boundary does not oscillate.
#define GHOST_CULL_HYST 256

// Per-mirror work tier, so hysteresis has state to compare against.
enum { GTIER_NEAR = 0, GTIER_MID = 1, GTIER_FAR = 2 };
static BYTE* g_mirrorTier = NULL;

// Rotating cursor so the FAR tier still refreshes a slice each frame.
static unsigned int g_cullRotor = 0;

// Which ids we have hidden a client-spawned legacy actor for.
static BYTE* g_hidLegacy = NULL;

// The MF_ bits this module added to each hidden actor, the only
// ones the un-hide clears.
static DWORD* g_hidAddedFlags = NULL;

// Ids whose linkage waits for the frame loop, out of the packet path.
static BYTE* g_hidRelinkPending = NULL;

// How many actors we hold hidden.
static unsigned int g_hidCount = 0;

// AActor::Tick early-outs vs real ticks, per window.
unsigned long long g_tickSkipped = 0, g_tickRan = 0;

// Prediction replay census: calls, depth, and the split between player logic
// and movement/collision.
unsigned long long g_predCalls = 0, g_predTicsSum = 0, g_predTicsMax = 0;
unsigned long long g_predThinkRaw = 0, g_predMoveRaw = 0;
static void ghost_ShowLegacy(unsigned int id, bool reveal);
static void ghost_ShowAllLegacy();
static void ghost_HideLegacy(unsigned int id, const AActor* pMirror, bool allowRelink);
static unsigned int		g_mirrorCount = 0;

static void ghost_Init()
{
	if (g_mirror == NULL)
	{
		g_mirror = new TObjPtr<AActor>[SNAP_MAXID];
		g_shadow = new GhostShadow[SNAP_MAXID];
		g_shadowLastState = new WORD[SNAP_MAXID];
		memset(g_shadowLastState, 0xFF, SNAP_MAXID * sizeof(WORD));
		g_recLastDrawn = new DWORD[SNAP_MAXID];
		memset(g_recLastDrawn, 0xFF, SNAP_MAXID * sizeof(DWORD));
		g_shadowLastTic = new int[SNAP_MAXID];
		memset(g_shadowLastTic, 0, SNAP_MAXID * sizeof(int));
		memset(g_shadow, 0, SNAP_MAXID * sizeof(GhostShadow));
		g_hadMirror = new BYTE[SNAP_MAXID];
		memset(g_hadMirror, 0, SNAP_MAXID);
		g_mirrorSyncedState = new WORD[SNAP_MAXID];
		memset(g_mirrorSyncedState, 0, SNAP_MAXID * sizeof(WORD));
		g_mirrorHardFrame = new WORD[SNAP_MAXID];
		memset(g_mirrorHardFrame, 0xFF, SNAP_MAXID * sizeof(WORD));
		g_wallTrace = new GhostWallTrace[SNAP_MAXID];
		memset(g_wallTrace, 0, SNAP_MAXID * sizeof(GhostWallTrace));
		g_smooth = new SnapSmooth[SNAP_MAXID];
		memset(g_smooth, 0, SNAP_MAXID * sizeof(SnapSmooth));
		g_slideTic = new int[SNAP_MAXID];
		memset(g_slideTic, 0, SNAP_MAXID * sizeof(int));
		g_mirrorEventSeq = new BYTE[SNAP_MAXID];
		memset(g_mirrorEventSeq, 0, SNAP_MAXID);
		g_hidLegacy = new BYTE[SNAP_MAXID];
		memset(g_hidLegacy, 0, SNAP_MAXID);
		g_hidAddedFlags = new DWORD[SNAP_MAXID];
		memset(g_hidAddedFlags, 0, SNAP_MAXID * sizeof(DWORD));
		g_hidRelinkPending = new BYTE[SNAP_MAXID];
		memset(g_hidRelinkPending, 0, SNAP_MAXID);
		g_mirrorTier = new BYTE[SNAP_MAXID];
		memset(g_mirrorTier, GTIER_NEAR, SNAP_MAXID);
		g_mirrorAnimTic = new WORD[SNAP_MAXID];
		memset(g_mirrorAnimTic, 0, SNAP_MAXID * sizeof(WORD));
		g_stLastAppearTic = new int[SNAP_MAXID];
		memset(g_stLastAppearTic, 0, SNAP_MAXID * sizeof(int));
		for (unsigned int i = 0; i < SNAP_MAXID; ++i)
			g_mirror[i] = NULL;
	}
}

// The engine's trail smoke classes through their replacements, resolved per
// level, as GetReplacement reads the skill.
static const PClass * g_pTrailSmoke[4] = { NULL, NULL, NULL, NULL };
static bool g_bTrailSmokeResolved = false;

static void ghost_ResolveTrailSmoke()
{
	for (unsigned int i = 0; i < 4; ++i)
		g_pTrailSmoke[i] = NULL;
	
	static const char* const szNames[2] = { "RocketSmokeTrail", "GrenadeSmokeTrail" };
	for (unsigned int i = 0; i < 2; ++i)
	{
		const PClass * pBase = PClass::FindClass(szNames[i]);
		g_pTrailSmoke[i * 2] = pBase;
		if ((pBase != NULL) && (pBase->ActorInfo != NULL))
		{
			FActorInfo * pRepl = pBase->ActorInfo->GetReplacement();
			if (pRepl != NULL)
				g_pTrailSmoke[i * 2 + 1] = pRepl->Class;
		}
	}
	g_bTrailSmokeResolved = true;
}

static bool ghost_IsTrailSmoke(const PClass * pCls)
{
	if (pCls == NULL)
		return false;
	
	if (g_bTrailSmokeResolved == false)
		ghost_ResolveTrailSmoke();

	for (unsigned int i = 0; i < 4; ++i)
	{
		if (g_pTrailSmoke[i] == NULL)
			continue;
	
		if ((pCls == g_pTrailSmoke[i]) || pCls->IsDescendantOf(g_pTrailSmoke[i]))
			return true;
	}
	return false;
}

// A pending snapshot point sound, by NetID and point, never a pointer.
struct GhostPointSound
{
	unsigned int	netID;
	fixed_t			x, y, z;
	int				channel;
	int				soundId;
	int				queuedTic;
};
static TArray<GhostPointSound> g_pointSounds;

//*****************************************************************************
//	COSMETIC EVENTS (design doc 6.3)
//
// The client's own copy of each routed spawn, run forward by its age.

static TMap<DThinker*, BYTE>	g_cosOwed;			// fresh copies still owed their age
static unsigned int				g_cosOwedNext = 0;	// what a spawn now is owed
static bool						g_cosFlagNext = false;	// the next spawn is a flagged copy
bool							g_bSnapCatchUp = false;
static unsigned int				g_cosSoundsHeld = 0;
struct CosSoundStarted
{
	AActor*	actor;
	int		soundId;
};
static TArray<CosSoundStarted>	g_cosSoundsStarted;	// this catch-up's CHAN_AUTO starts
static unsigned int				g_cosEvents = 0, g_cosCopies = 0, g_cosPlain = 0, g_cosBloodEvents = 0;
static unsigned int				g_cosNoClass = 0, g_cosBadState = 0, g_cosNoOriginator = 0, g_cosOtherMap = 0;
static unsigned int				g_cosCatchUpTicks = 0;
static TMap<const PClass*, unsigned int>	g_cosByClass;	// copies this window

// TMap leaves a new plain value uninitialized.
static void ghost_CountClass(TMap<const PClass*, unsigned int>& counts, const PClass* pType)
{
	unsigned int* pCount = counts.CheckKey(pType);
	if (pCount != NULL)
		++*pCount;
	else
		counts[pType] = 1;
}

// The three largest counts, " Name n" each, and the map cleared.
static FString ghost_TopClasses(TMap<const PClass*, unsigned int>& counts)
{
	FString top;
	for (unsigned int n = 0; n < 3; ++n)
	{
		const PClass* pBest = NULL;
		unsigned int best = 0;
		TMapIterator<const PClass*, unsigned int> it(counts);
		TMap<const PClass*, unsigned int>::Pair* pPair;
		while (it.NextPair(pPair))
			if (pPair->Value > best)
			{
				best = pPair->Value;
				pBest = pPair->Key;
			}
		if (pBest == NULL)
			break;
		top.AppendFormat(" %s %u", pBest->TypeName.GetChars(), best);
		counts.Remove(pBest);
	}
	counts.Clear();
	return top.IsEmpty() ? FString(" none") : top;
}

static void cos_Owe(AActor* pActor, unsigned int age)
{
	if ((pActor != NULL) && (age > 0))
		g_cosOwed.Insert(pActor, (BYTE)age);
}

//*****************************************************************************
//	MIRROR SPAWNS (design doc 6.3)
//
// A mirror runs the client-side spawner actions of the states it enters.

bool g_bSnapMirrorSpawns = false;

static const unsigned int MS_MAXOWED = 8;	// as the per-frame advance's clamp

// The ring (SnapMsRing) and what the mirror needs beside it.
struct MsRing : SnapMsRing<const FState*>
{
	const PClass*	type;
	int				lastAnim;		// gametic of the last per-frame advance
	bool			bSeed;			// not animating until now: the next record seeds
};
static TMap<unsigned int, MsRing>			g_msRings;		// by id, spawner classes only
static TMap<const FState*, bool>			g_msStateOk;	// a spawner whose owner runs no special
static TMap<const PClass*, unsigned int>	g_msByClass;	// spawned this window
static int									g_msSetOwed = -1;	// ghost_SetStateSafe spawns, owed this
static unsigned int g_msActions = 0, g_msSpawned = 0, g_msOwedTics = 0;
static unsigned int g_msJump = 0, g_msResync = 0, g_msHold = 0, g_msSeed = 0;
static unsigned int g_msBehind = 0, g_msUndecided = 0, g_msHard = 0, g_msUnnamed = 0;

// The missile's target (design doc 9.3, Known limits): resolutions, and
// A_CustomMissile run by a mirror with none, by caster.
static AActor*								g_msSelf = NULL;	// the mirror ms_SetStateSpawning runs
static unsigned int g_tgtAtEnter = 0, g_tgtLate = 0, g_tgtNever = 0;
static unsigned int g_tgtNoneMissile = 0, g_tgtNoneOther = 0, g_tgtNoneUnknown = 0;
static TMap<const PClass*, unsigned int>	g_tgtNoneByClass;

enum { MS_SEED, MS_RESYNC, MS_BEHIND, MS_UNDECIDED, MS_JUMP };

// The state's owner, not the mirror's class: a goto can reach another class's
// states, whose parameter expressions that class owns.
bool zx::netsnapshot::MirrorSpawnAction(const FState* pState)
{
	if (IsSpawnerState(pState) == false)
		return false;
	const bool* pKnown = g_msStateOk.CheckKey(pState);
	const bool bOk = (pKnown != NULL) ? *pKnown
		: (g_msStateOk[pState] = (OwnsSpecialExpression(FState::StaticFindStateOwner(pState)) == false));
	if (bOk)
	{
		++g_msActions;
		// Counted only where the action would have spawned with a target.
		if ((g_msSelf != NULL) && (g_msSelf->target == NULL))
		{
			const int need = CustomMissileNeedsTarget(g_msSelf, pState);
			if (need == 1)
			{
				if (g_msSelf->flags & MF_MISSILE)
					++g_tgtNoneMissile;
				else
					++g_tgtNoneOther;
				ghost_CountClass(g_tgtNoneByClass, g_msSelf->GetClass());
			}
			else if (need == 2)
				++g_tgtNoneUnknown;
		}
	}
	return bOk;
}

// The mirror's ring, for a spawner class; NULL for any other.
static MsRing* ms_Ring(unsigned int id, const AActor* pActor)
{
	const PClass* const pType = pActor->GetClass();
	if (ClassSpawns(pType) == false)
		return NULL;
	MsRing* pRing = g_msRings.CheckKey(id);
	if ((pRing != NULL) && (pRing->type == pType))
		return pRing;
	MsRing& ring = g_msRings[id];
	memset(&ring, 0, sizeof(ring));
	ring.type = pType;
	ring.bSeed = true;
	return &ring;
}

// pState and the zero-tic states SetState walked from it to pLand.
static void ms_PushChain(MsRing& ring, const FState* pState, const FState* pLand, int tic)
{
	for (unsigned int n = 0; (n < SNAP_MS_RING) && (pState != NULL); ++n)
	{
		ComputeMsPush(ring, pState, tic);
		if ((pState == pLand) || (pState->Tics != 0) || (pState->TicRange != 0))
			return;
		pState = pState->GetNextState();
	}
}

// Whether the default chain from pFrom can enter pTo within budget tics, a
// doubt counting as reachable.
static bool ms_Reaches(const FState* pFrom, int ticsLeft, const FState* pTo, int budget)
{
	if ((pFrom == NULL) || (ticsLeft < 0))
		return false;
	int spent = ticsLeft;
	const FState* p = pFrom;
	for (unsigned int n = 0; n < 64; ++n)
	{
		if (spent > budget)
			return false;
		p = p->GetNextState();
		if ((p == NULL) || (p->Tics < 0))
			return (p == pTo) && (p != NULL);
		if (p == pTo)
			return true;
		spent += p->Tics;
	}
	return false;
}

// The note's order, after the terminal hold its callers apply.
static int ms_Classify(const MsRing& ring, const AActor* pActor, const FState* pWire, int recTic, int age)
{
	if (ring.bSeed || (ring.count == 0))
		return MS_SEED;
	const int longest = (pWire->Tics < 0) ? -1 : (pWire->Tics + pWire->TicRange);
	if (ComputeMsResync(ring, pWire, longest, recTic))
		return MS_RESYNC;
	if (ms_Reaches(pActor->state, pActor->tics, pWire, age + 8))
		return MS_BEHIND;
	const SnapMsEntry<const FState*>& oldest = ComputeMsOldest(ring);
	if (ms_Reaches(oldest.state, oldest.state->Tics, pWire, age + SNAP_MS_SLACK))
		return MS_UNDECIDED;
	return MS_JUMP;
}

// SetState with the mirror's spawners run, owed tics ago: the mirror stands
// where it was then, and what it spawns is run forward by the rest.
static bool ms_SetStateSpawning(AActor* pActor, FState* pState, unsigned int owed)
{
	const fixed_t x = pActor->x, y = pActor->y, z = pActor->z;
	pActor->x -= pActor->velx * (int)owed;
	pActor->y -= pActor->vely * (int)owed;
	pActor->z -= pActor->velz * (int)owed;
	const unsigned int owedNext = g_cosOwedNext;
	g_cosOwedNext = owed;
	g_bSnapMirrorSpawns = true;
	g_msSelf = pActor;
	const bool bAlive = pActor->SetState(pState, true);
	g_msSelf = NULL;
	g_bSnapMirrorSpawns = false;
	g_cosOwedNext = owedNext;
	pActor->x = x;
	pActor->y = y;
	pActor->z = z;
	return bAlive;
}

static void ms_Forget(unsigned int id)
{
	if (g_msRings.CountUsed() > 0)
		g_msRings.Remove(id);
}

void zx::netsnapshot::CosmeticSpawn(AActor* pActor)
{
	if (g_cosFlagNext && (pActor != NULL))
	{
		pActor->NetworkFlags |= NETFL_CLIENTSIDEONLY;
		g_cosFlagNext = false;
	}
	if (g_bSnapMirrorSpawns && (pActor != NULL))
	{
		++g_msSpawned;
		g_msOwedTics += g_cosOwedNext;
		ghost_CountClass(g_msByClass, pActor->GetClass());
	}
	cos_Owe(pActor, g_cosOwedNext);
}

void zx::netsnapshot::CosmeticForget(AActor* pActor)
{
	if (g_cosOwed.CountUsed() > 0)
		g_cosOwed.Remove(pActor);
}

void zx::netsnapshot::CosmeticFirstTick(DThinker* pThinker)
{
	if (g_cosOwed.CountUsed() == 0)
		return;
	const BYTE* pAge = g_cosOwed.CheckKey(pThinker);
	if (pAge == NULL)
		return;
	const unsigned int age = *pAge;
	g_cosOwed.Remove(pThinker);
	if (pThinker->ObjectFlags & OF_EuthanizeMe)
		return;

	// TickThinkers' own Tick follows, so the copy ends the tic age tics older.
	AActor* const pActor = static_cast<AActor*>(pThinker);	// only actors are owed
	const bool bCatchUp = g_bSnapCatchUp;
	const unsigned int owedNext = g_cosOwedNext;
	const unsigned int started = g_cosSoundsStarted.Size();
	g_bSnapCatchUp = true;
	for (unsigned int k = 0; (k < age) && ((pActor->ObjectFlags & OF_EuthanizeMe) == 0); ++k)
	{
		g_cosOwedNext = age - k - 1;
		pActor->Tick();
		++g_cosCatchUpTicks;
	}
	g_cosOwedNext = owedNext;
	g_bSnapCatchUp = bCatchUp;
	g_cosSoundsStarted.Resize(started);
}

// By the actor making the sound: a missile spawned in the catch-up plays its
// own SeeSound.
// Counted in cl_main.cpp beside the report that prints them, so the declaration sits at file
// scope here rather than inside the namespace below.
extern unsigned int g_sndLocalGhost, g_sndLocalOther, g_sndLocalUngated;

// Item respawns answered behind a mirror, for the census line.
static unsigned int g_respawnOnMirror = 0;

bool zx::netsnapshot::RespawnBehindMirror(AActor* pActor, bool bFog)
{
	if ((pActor == NULL) || ((pActor->STFlags & STFL_SNAPGHOSTHIDDEN) == 0))
		return false;

	// The record draws this item, so the hidden copy keeps its state and its flags:
	// only the sound and the fog are made, at the ghost it sits behind.
	if (pActor->IsKindOf(RUNTIME_CLASS(AInventory)) == false
		|| static_cast<AInventory *>(pActor)->DoRespawn() == false)
		return true;
	++g_respawnOnMirror;
	if (bFog)
	{
		fixed_t x = pActor->x, y = pActor->y, z = pActor->z;
		GhostPosition(pActor->NetID, x, y, z);
		S_Sound(x, y, z, CHAN_VOICE, "misc/spawn", 1, ATTN_IDLE);
		Spawn("ItemFog", x, y, z, ALLOW_REPLACE);
	}
	return true;
}

// The server sends this entity's sounds, so a client playing them itself would double them.
// True to skip: the role test, the counters and the switch all live here.
bool zx::netsnapshot::GhostSoundHandled(AActor* pActor)
{
	if ((NETWORK_InClientMode() == false) || (pActor == NULL)
		|| (pActor->NetID == 0) || (pActor->player != NULL))
		return false;

	if (GhostPresent(pActor->NetID) == false)
	{
		++g_sndLocalOther;
		return false;
	}

	// Only while the server routes this entity's sounds, which is level 3 alone: below it
	// stock still sends them, and its own Execute plays them through this very path.
	if ((ServerGatesLegacy() == false) || (fua_csnap_nolocalsound == false))
	{
		++g_sndLocalUngated;
		return false;
	}

	++g_sndLocalGhost;
	return true;
}

bool zx::netsnapshot::CatchUpHoldsSound(AActor* pActor, int channel, int soundId)
{
	// Only while an actor is being run forward by its age, and only its CHAN_AUTO sound: the
	// flag and the channel test live here so the s_sound.cpp site is one call.
	if ((g_bSnapCatchUp == false) || (pActor == NULL) || ((channel & 7) != CHAN_AUTO))
		return false;

	for (unsigned int i = 0; i < g_cosSoundsStarted.Size(); ++i)
	{
		if ((g_cosSoundsStarted[i].actor == pActor) && (g_cosSoundsStarted[i].soundId == soundId))
		{
			++g_cosSoundsHeld;
			return true;
		}
	}
	CosSoundStarted s;
	s.actor = pActor;
	s.soundId = soundId;
	g_cosSoundsStarted.Push(s);
	return false;
}

// Depth up the class's ancestor chain, then an offset into that owner's states.
static FState* cos_StateFromCode(const PClass* pType, unsigned int code)
{
	unsigned int depth = code >> 12;
	const unsigned int offset = code & 0xFFF;
	const PClass* pOwner = pType;
	while ((depth > 0) && (pOwner != NULL))
	{
		pOwner = pOwner->ParentClass;
		--depth;
	}
	if ((pOwner == NULL) || (pOwner->ActorInfo == NULL)
		|| (offset >= (unsigned int)pOwner->ActorInfo->NumOwnedStates))
		return NULL;
	return &pOwner->ActorInfo->OwnedStates[offset];
}

// The originator as SpawnBlood resolves it: the real actor, else the mirror.
static AActor* cos_Originator(unsigned int netID)
{
	if (netID == 0)
		return NULL;
	AActor* pActor = CLIENT_FindThingByNetID((unsigned short)netID);
	return (pActor != NULL) ? pActor : MirrorForNetID(netID);
}

void zx::netsnapshot::CosmeticEvents(BYTESTREAM_s* pByteStream)
{
	// Read whole either way; a datagram from another map makes nothing.
	const BYTE stamp = (BYTE)pByteStream->ReadByte();
	const unsigned int count = (WORD)pByteStream->ReadShort();
	const bool bMake = (gamestate == GS_LEVEL) && MapStampCurrent(stamp);
	for (unsigned int e = 0; e < count; ++e)
	{
		const int flags = pByteStream->ReadByte();
		const int kind = flags & SNAP_COSE_KIND_MASK;
		const unsigned int age = (flags & SNAP_COSE_AGE_MASK) >> SNAP_COSE_AGE_SHIFT;

		if ((kind == SNAP_COSE_COPY) || (kind == SNAP_COSE_PLAIN))
		{
			SnapCosGeneric g;
			SnapCosCursor cur = { pByteStream->pbStream, pByteStream->pbStreamEnd };
			CosReadGeneric(cur, ClientMapFrame(), flags, g);
			pByteStream->pbStream += cur.p - pByteStream->pbStream;
			if (pByteStream->pbStream > pByteStream->pbStreamEnd)
				break;
			++g_cosEvents;
			if (bMake == false)
			{
				++g_cosOtherMap;
				continue;
			}

			const PClass* pType = NETWORK_GetClassFromIdentification(g.classId);
			if ((pType == NULL) || (pType->IsDescendantOf(RUNTIME_CLASS(AActor)) == false))
			{
				++g_cosNoClass;
				continue;
			}
			AActor* pCopy = Spawn(pType, g.x, g.y, g.z, NO_REPLACE);
			if (pCopy == NULL)
				continue;
			if (kind == SNAP_COSE_COPY)
				pCopy->NetworkFlags |= NETFL_CLIENTSIDEONLY;
			else
				++g_cosPlain;
			pCopy->angle = g.angle;
			pCopy->velx = g.vel[0];
			pCopy->vely = g.vel[1];
			pCopy->velz = g.vel[2];
			if (g.extras & SNAP_COSX_TRANSLATION)
				pCopy->Translation = TRANSLATION(g.transType, g.transIndex);
			if (g.extras & SNAP_COSX_ALPHA)
				pCopy->alpha = g.alpha;
			if (g.extras & SNAP_COSX_SCALE)
			{
				pCopy->scaleX = g.scaleX;
				pCopy->scaleY = g.scaleY;
			}
			if (g.extras & SNAP_COSX_STYLE)
				pCopy->RenderStyle.AsDWORD = g.style;
			// With its action, owed the copy's age, but not after a hold.
			if (g.stateCode >= 0)
			{
				FState* const pState = cos_StateFromCode(pType, (unsigned int)g.stateCode);
				if (pState == NULL)
					++g_cosBadState;
				else
				{
					const unsigned int owedNext = g_cosOwedNext;
					g_cosOwedNext = age;
					const bool bAlive = pCopy->SetState(pState, (g.extras & SNAP_COSX_ACTED) != 0);
					g_cosOwedNext = owedNext;
					if (bAlive == false)
						continue;
				}
			}
			++g_cosCopies;
			ghost_CountClass(g_cosByClass, pType);
			cos_Owe(pCopy, age);
		}
		else
		{
			SnapCosBlood b;
			SnapCosCursor cur = { pByteStream->pbStream, pByteStream->pbStreamEnd };
			CosReadBlood(cur, ClientMapFrame(), flags, b);
			pByteStream->pbStream += cur.p - pByteStream->pbStream;
			if (pByteStream->pbStream > pByteStream->pbStreamEnd)
				break;
			++g_cosEvents;
			++g_cosBloodEvents;
			if (bMake == false)
			{
				++g_cosOtherMap;
				continue;
			}

			AActor* const pOriginator = cos_Originator(b.originator);
			if (pOriginator == NULL)
			{
				++g_cosNoOriginator;
				continue;
			}
			// Flagged and owed as it spawns: P_SpawnBlood picks the state by
			// damage with SetState, whose action runs at once.
			const unsigned int owedNext = g_cosOwedNext;
			g_cosFlagNext = ((flags & SNAP_COSE_BLOODPLAIN) == 0);
			g_cosOwedNext = age;
			AActor* const pBlood = (kind == SNAP_COSE_BLOOD)
				? P_SpawnBlood(b.x, b.y, b.z, b.dir, b.damage, pOriginator)
				: (flags & SNAP_COSE_SPLATTER2) ? P_BloodSplatter2(b.x, b.y, b.z, pOriginator)
				: P_BloodSplatter(b.x, b.y, b.z, pOriginator);
			g_cosFlagNext = false;
			g_cosOwedNext = owedNext;
			if (pBlood == NULL)
				continue;	// cl_bloodtype drew particles only
			if (flags & SNAP_COSE_BLOODPLAIN)
				++g_cosPlain;
			++g_cosCopies;
			ghost_CountClass(g_cosByClass, pBlood->GetClass());
		}
	}
	if (pByteStream->pbStream > pByteStream->pbStreamEnd)
		CLIENT_PrintWarning("SnapCosmetic: Packet contained %td too few bytes\n",
			pByteStream->pbStream - pByteStream->pbStreamEnd);
}

void zx::netsnapshot::CosmeticCountsReset()
{
	g_cosEvents = g_cosBloodEvents = g_cosCopies = g_cosPlain = 0;
	g_cosNoClass = g_cosNoOriginator = g_cosOtherMap = g_cosBadState = 0;
	g_cosByClass.Clear();
}

void zx::netsnapshot::LevelUnloaded()
{
	g_pointSounds.Clear();
	g_cosOwed.Clear();
	g_msRings.Clear();

	// The teardown goes into any recording first (9.2).
	if (RecorderActive())
		RecorderReset("level unloaded");

	// Class replacements are skill-dependent and skill can change with the map.
	g_bTrailSmokeResolved = false;

	// Shadows are keyed by id, and must not outlive the map (9.2).
	if (g_shadow != NULL)
	{
		memset(g_shadow, 0, SNAP_MAXID * sizeof(GhostShadow));
		memset(g_shadowLastState, 0xFF, SNAP_MAXID * sizeof(WORD));
		if (g_recLastDrawn != NULL)
			memset(g_recLastDrawn, 0xFF, SNAP_MAXID * sizeof(DWORD));
		memset(g_shadowLastTic, 0, SNAP_MAXID * sizeof(int));
	}

	if (g_mirror != NULL)
	{
		// Only the listed entries, as the pointers cannot be inspected now.
		for (unsigned int k = 0; k < g_mirrorIds.Size(); ++k)
			*(&g_mirror[g_mirrorIds[k]]) = NULL;
	}
	g_mirrorIds.Clear();
	g_mirrorCount = 0;
	g_cullRotor = 0;

	// Anything keyed on entity id has a map lifetime (9.2).
	if (g_mirrorHardFrame != NULL)
		 memset(g_mirrorHardFrame, 0xFF, SNAP_MAXID * sizeof(WORD));
	// The map's lines, not the ids, are what a cached trace depends on.
	if (g_wallTrace != NULL)
		memset(g_wallTrace, 0, SNAP_MAXID * sizeof(GhostWallTrace));
	if (g_smooth != NULL)
		memset(g_smooth, 0, SNAP_MAXID * sizeof(SnapSmooth));
	if (g_mirrorEventSeq != NULL)
		 memset(g_mirrorEventSeq, 0, SNAP_MAXID);
	
	if (g_mirrorSyncedState != NULL)
		memset(g_mirrorSyncedState, 0, SNAP_MAXID * sizeof(WORD));

	// No un-hiding here: these actors are about to be destroyed with the
	// level, and their pointers must not be touched (see above).
	if (g_hidLegacy != NULL)
		memset(g_hidLegacy, 0, SNAP_MAXID);
	if (g_mirrorTier != NULL)
		memset(g_mirrorTier, GTIER_NEAR, SNAP_MAXID);
	if (g_mirrorAnimTic != NULL)
		memset(g_mirrorAnimTic, 0, SNAP_MAXID * sizeof(WORD));

	// The ghost table is keyed on NetIDs that the server is about to reissue to
	// entirely different actors, so it has to go at the same moment.
	ForgetWorld();
}

// Tics after which a pending sound's re-attach counts as late,
// kept until the sound ends.
#define GHOST_POINTSOUND_LATE	35
#define GHOST_POINTSOUND_MAX	1024
static unsigned int g_ptSndOnMirror = 0, g_ptSndAttached = 0, g_ptSndAttachedLate = 0;
static unsigned int g_ptSndRefused = 0, g_ptSndEnded = 0, g_ptSndCap = 0, g_ptSndBusy = 0;

void CLIENT_TraceSoundEvent(int soundId, int channel, const char *result);	// cl_main.cpp

void zx::netsnapshot::PointSoundPending(unsigned int netID, fixed_t x, fixed_t y, fixed_t z,
	int channel, int soundId, bool bStarted)
{
	if ((netID == 0) || (netID >= SNAP_MAXID))
		return;
	// Not started by the S_Sound just made: refused at the start (the near
	// limit, or anything else S_StartSound turns away), so nothing to move.
	if (bStarted == false)
	{
		++g_ptSndRefused;
		CLIENT_TraceSoundEvent(soundId, channel, "refused at the start");
		return;
	}
	if (g_pointSounds.Size() >= GHOST_POINTSOUND_MAX)
	{
		++g_ptSndCap;	// not tracked; it plays out at the point
		CLIENT_TraceSoundEvent(soundId, channel, "not tracked, list full");
		return;
	}
	GhostPointSound ps;
	ps.netID = netID;
	ps.x = x;
	ps.y = y;
	ps.z = z;
	ps.channel = channel;
	ps.soundId = soundId;
	ps.queuedTic = gametic;
	g_pointSounds.Push(ps);
}

void zx::netsnapshot::CountPointSoundOnMirror()
{
	++g_ptSndOnMirror;
}

static void ghost_AttachPointSounds()
{
	for (unsigned int i = 0; i < g_pointSounds.Size(); )
	{
		const GhostPointSound &ps = g_pointSounds[i];
		AActor *pMirror = MirrorForNetID(ps.netID, false);
		const char *pszResult = NULL;
		if (pMirror != NULL)
		{
			const int r = S_AttachPointSound(ps.x, ps.y, ps.z, ps.channel, ps.soundId, pMirror);
			if (r > 0)
			{
				if (gametic - ps.queuedTic > GHOST_POINTSOUND_LATE)
				{
					++g_ptSndAttachedLate;
					pszResult = "re-attached to the mirror, late";
				}
				else
				{
					++g_ptSndAttached;
					pszResult = "re-attached to the mirror";
				}
			}
			else if (r < 0)
			{
				++g_ptSndBusy;
				pszResult = "left at the point, mirror channel busy";
			}
			else
			{
				++g_ptSndEnded;
				pszResult = "ended at the point";
			}
		}
		else if (S_AttachPointSound(ps.x, ps.y, ps.z, ps.channel, ps.soundId, NULL) == 0)
		{
			++g_ptSndEnded;
			pszResult = "ended at the point";
		}

		if (pszResult == NULL)
		{
			++i;
			continue;
		}
		CLIENT_TraceSoundEvent(ps.soundId, ps.channel, pszResult);
		g_pointSounds.Delete(i);
	}
}

void zx::netsnapshot::GhostClearAll()
{
	// With the ghosts: across a map change an id is handed out again, and a set left here
	// would dress whatever takes it.
	g_ghostProps.Clear();
	g_pointSounds.Clear();
	g_msRings.Clear();
	if (g_mirror == NULL)
		return;

	ghost_ShowAllLegacy();

	for (unsigned int k = 0; k < g_mirrorIds.Size(); ++k)
	{
		const unsigned int id = g_mirrorIds[k];
		if (g_mirror[id] != NULL)
		{
			// Clear the GC pin (set in ghost_CreateMirror) so this actor we are
			// deliberately destroying can actually be collected.
			g_mirror[id]->ObjectFlags &= ~OF_Fixed;
			g_mirror[id]->Destroy();
			g_mirror[id] = NULL;
		}
	}
	g_mirrorIds.Clear();
	g_mirrorCount = 0;
}

// Each class's Speed in map units a tic, cached by classId (0 unknown).
static TArray<unsigned int> g_classSpeedCache;

// Whether the real actor of this class would be solid and in the blockmap, from
// the class defaults, since every mirror's own flags say no.
static bool ghost_ClassCollides(unsigned int classId)
{
	const PClass * pType = NETWORK_GetClassFromIdentification((unsigned short)classId);
	if ((pType == NULL) || (pType->IsDescendantOf(RUNTIME_CLASS(AActor)) == false))
		return false;
	const AActor * pDef = GetDefaultByType(pType);
	if (pDef == NULL)
		return false;
	return ((pDef->flags & MF_SOLID) != 0)
		&& ((pDef->flags & (MF_NOBLOCKMAP | MF_NOCLIP)) == 0);
}

static unsigned int ghost_SpeedForClass(unsigned int classId)
{
	if (classId >= g_classSpeedCache.Size())
	{
		const unsigned int was = g_classSpeedCache.Size();
		g_classSpeedCache.Resize(classId + 64);
		for (unsigned int i = was; i < g_classSpeedCache.Size(); ++i)
			g_classSpeedCache[i] = 0xFFFFFFFF;		// not yet resolved
	}

	unsigned int& slot = g_classSpeedCache[classId];
	if (slot != 0xFFFFFFFF)
		return slot;

	slot = 0;
	const PClass * pType = NETWORK_GetClassFromIdentification((unsigned short)classId);
	if ((pType != NULL) && (pType->IsDescendantOf(RUNTIME_CLASS(AActor))))
	{
		const AActor * pDef = GetDefaultByType(pType);
		if (pDef != NULL)
		{
			// Speed bounds how far it could move, a missile's or a walker's
			// step, 0 falling back to the time test.
			// [Kaori-Chani] >> keeps the fixed type; the whole units are what is wanted.
			const int units = (int)(pDef->Speed >> FRACBITS);
			if (units > 0)
				slot = (unsigned int)units;
		}
	}
	return slot;
}

// Public wrapper, for the replay's glide census.
unsigned int zx::netsnapshot::SpeedForClass(unsigned int classId)
{
	return ghost_SpeedForClass(classId);
}

// Creates one ghost's hollow mirror at its current position, as a far mirror
// may wait for its pose.
static AActor* ghost_CreateMirror(unsigned int id, unsigned short classId,
	fixed_t x, fixed_t y, fixed_t z)
{
	const PClass* pType = NETWORK_GetClassFromIdentification(classId);
	if (pType == NULL)
		return NULL;

	AActor* pActor = Spawn(pType, x, y, z, NO_REPLACE);
	if (pActor == NULL)
		return NULL;
	// A new mirror, including one recreated after a hard resync, draws its
	// target: nothing to interpolate from.
	if ((g_smooth != NULL) && (id < SNAP_MAXID))
		g_smooth[id].valid = false;

	// Parked where the thinker loop never runs it.
	pActor->ChangeStatNum(STAT_INFO);

	// Unlinked before MF_NOBLOCKMAP is set, which gates the unlink, keeping
	// only the sector link.
	pActor->UnlinkFromWorld();
	pActor->flags |= MF_NOBLOCKMAP | MF_NOGRAVITY | MF_NOCLIP;
	pActor->flags5 |= MF5_NOINTERACTION;
	pActor->LinkToWorld();

	// The gameplay guards' mark, apart from the flags collision clears.
	pActor->NetworkFlags |= NETFL_MIRROR;

	// A mirror has no network identity of its own; it must never be confused
	// with a server-driven actor or free a net ID belonging to something else.
	pActor->NetID = 0;

	// Pinned against the collector, as g_mirror is no root, until a
	// deliberate Destroy clears it.
	pActor->ObjectFlags |= OF_Fixed;

	// Built with whatever the server has already said about this entity, which is what a
	// joiner and a re-enter need: the event may be long past.
	{
		const SnapProps * pProps = g_ghostProps.CheckKey(id);
		if (pProps != NULL)
		{
			ghost_ApplyPropsTo(pActor, *pProps);
			++g_propsToMirror;
		}
	}

	g_mirror[id] = pActor;
	g_mirrorIds.Push(id);
	ms_Forget(id);

	// Fresh mirror, fresh tier: a recycled id must not inherit FAR and be
	// skipped by the rotor on its first frame.
	if (g_mirrorTier != NULL)
		g_mirrorTier[id] = GTIER_NEAR;
	if (g_mirrorAnimTic != NULL)
		g_mirrorAnimTic[id] = (WORD)gametic;
	// Instruments have lifetimes too: a gap differenced across a slot reuse
	// describes two unrelated actors.
	if (g_stLastAppearTic != NULL)
		g_stLastAppearTic[id] = 0;

	// The spawn state is the synced one, as a state index.
	g_mirrorSyncedState[id] = QStateForState(pActor->GetClass(), pActor->state);
	++g_mirrorCount;
	return pActor;
}

//*****************************************************************************
//	HIDING THE LEGACY COPY
//
// Below level 3 a server-made legacy copy stands beside each ghost, hidden
// rather than destroyed, since the server still addresses it.

// Whether a player ghost must keep its own actor, as two do.
static bool ghost_ExcludedPlayer(unsigned int id)
{
	if ((id == 0) || (id > MAXPLAYERS))
		return false;

	// The local player, whose prediction ticks it, or every player when
	// it cannot be identified.
	const unsigned int local = IDList<AActor>::reservedIDForPlayer(consoleplayer);
	if ((local == 0) || (id == local))
		return true;

	// And whoever is viewed through, whose real actor the camera hides.
	const AActor* pCam = players[consoleplayer].camera;
	if ((pCam != NULL) && (pCam->player != NULL))
	{
		const int viewed = (int)(pCam->player - players);
		return (id == IDList<AActor>::reservedIDForPlayer(viewed));
	}
	return false;
}

// Player ghosts seen and skipped on the last frame.
static unsigned int g_playerGhostSeen = 0, g_playerGhostExcluded = 0;

// Ends a legacy copy's hide, showing it only when `reveal` (not for
// a ghost that has left).
static void ghost_ShowLegacy(unsigned int id, bool reveal)
{
	if ((g_hidLegacy == NULL) || (g_hidLegacy[id] == 0))
		return;

	AActor* pReal = (reveal) ? g_ActorNetIDList.findPointerByID((unsigned short)id) : NULL;
	if (pReal != NULL)
	{
		pReal->renderflags &= ~RF_INVISIBLE;
		// Resume ticking: the actor is authoritative again.
		pReal->STFlags &= ~STFL_SNAPGHOSTHIDDEN;
		// Relinked, clearing only the bits the hide added.
		const DWORD added = (g_hidAddedFlags != NULL) ? g_hidAddedFlags[id] : 0;
		if (added != 0)
		{
			pReal->UnlinkFromWorld();
			// [Kaori-Chani] flags is a typed set here, so the recorded mask is named as one, as in
			// the matching add below.
			pReal->flags &= ~ActorFlags::FromInt(added);
			pReal->LinkToWorld();
		}
	}
	if (g_hidAddedFlags != NULL)
		g_hidAddedFlags[id] = 0;
	if (g_hidRelinkPending != NULL)
		g_hidRelinkPending[id] = 0;
	if (g_hidLegacy[id] && (g_hidCount > 0))
		--g_hidCount;
	g_hidLegacy[id] = 0;
}

static void ghost_ShowAllLegacy()
{
	if (g_hidLegacy == NULL)
		return;
	for (unsigned int id = 0; id < SNAP_MAXID; ++id)
	{
		if (g_hidLegacy[id])
			ghost_ShowLegacy(id, true);
	}
}

// Hides one legacy actor a ghost draws, `allowRelink` false deferring the
// linkage to the frame loop.
static void ghost_HideLegacy(unsigned int id, const AActor* pMirror, bool allowRelink)
{
	if (g_hidLegacy == NULL)
		return;

	AActor* pReal = g_ActorNetIDList.findPointerByID((unsigned short)id);

	if ((pReal == NULL) || (pReal == pMirror) || ghost_ExcludedPlayer(id))
		return;

	pReal->renderflags |= RF_INVISIBLE;

	// Stop it ticking: the mirror carries its motion and animation,
	// so its own Tick is pure cost.
	pReal->STFlags |= STFL_SNAPGHOSTHIDDEN;

	if (g_hidLegacy[id] == 0)
	{
		g_hidLegacy[id] = 1;
		++g_hidCount;
	}

	// Out of the sector and blockmap lists, by only the bits the
	// actor did not already have.
	DWORD add = 0;
	if ((pReal->flags & MF_NOSECTOR) == 0)
		add |= MF_NOSECTOR;

	if (add == 0)
		return;

	// The list surgery waits for the frame loop.
	if (allowRelink == false)
	{
		if (g_hidRelinkPending != NULL)
			g_hidRelinkPending[id] = 1;
		return;
	}

	// One relink for both bits: LinkToWorld reads the flags, so the
	// unlink/modify/link sequence has to bracket every change.
	pReal->UnlinkFromWorld();
	pReal->flags |= ActorFlags::FromInt(add);	// [Kaori-Chani] a typed set, not a DWORD
	pReal->LinkToWorld();
	// [Kaori-Chani] The unlink saved touching_sectorlist for a relink MF_NOSECTOR then
	// skips: freed here, or the actor stays in every sector's touching list.
	P_DelSector_List();
	if (g_hidAddedFlags != NULL)
		g_hidAddedFlags[id] |= add;
	if (g_hidRelinkPending != NULL)
		g_hidRelinkPending[id] = 0;
}

// Mirror actor for a NetID, or NULL.
unsigned int g_mirrorLookupNoTable = 0, g_mirrorLookupBadId = 0;
unsigned int g_mirrorLookupEmpty = 0, g_mirrorLookupOk = 0;

// Measure local animation against what the server sent.
static unsigned int g_animPredictOk = 0, g_animPredictMiss = 0;

// Event-triggered phase refreshes the frame test would have skipped.
static unsigned int g_evPhaseForced = 0;

// Remote players' own actors repositioned from their ghost, and the tic that
// last happened on.
static unsigned int g_realPosed = 0;
static int g_realPoseTic = -1;
// The same populations by render tier.
static unsigned int g_animTierOk[3] = { 0, 0, 0 };
static unsigned int g_animTierMiss[3] = { 0, 0, 0 };
static unsigned int g_animBehind[5] = { 0, 0, 0, 0, 0 };	// index 1..4, 4 = "4 or more"
static unsigned int g_animOffChain = 0;
static unsigned int g_animAhead = 0;		// mirror ran past the wire's frame
// Corrections withheld from a mirror resting in a final state the wire's
// state leads to, by the normal resync and by the hard one.
static unsigned int g_animTerminalHeld = 0, g_animTerminalHeldHard = 0;

// The player a mirror stands in for, or -1, by walking the reserved range.
int zx::netsnapshot::MirrorPlayerNum(const AActor* pActor)
{
	if ((g_mirror == NULL) || (pActor == NULL))
		return -1;
	for (unsigned int p = 0; p < (unsigned int)MAXPLAYERS; ++p)
	{
		if (g_mirror[p + 1] == pActor)
			return (int)p;
	}
	return -1;
}

AActor* zx::netsnapshot::MirrorForNetID(unsigned int id, bool bCount)
{
	if (g_mirror == NULL)
	{
		if (bCount)
			++g_mirrorLookupNoTable;
		return NULL;
	}
	if (id >= SNAP_MAXID)
	{
		if (bCount)
			++g_mirrorLookupBadId;
		return NULL;
	}

	// Read through TObjPtr's barrier once and keep the result: calling the
	// conversion operator twice can legitimately give two different answers.
	AActor* pMirror = g_mirror[id];
	if (bCount == false)
		return pMirror;
	if (pMirror == NULL)
		++g_mirrorLookupEmpty;
	else
		++g_mirrorLookupOk;
	return pMirror;
}

// Hides the legacy actors for what the delta just applied brought.
void zx::netsnapshot::HideEnteredLegacy()
{
	if ((g_hidLegacy == NULL) || (fua_csnap_ghosts == false))
		return;

	unsigned int count = 0;
	const unsigned int* pIds = EnteredIds(count);
	if (pIds == NULL)
		return;

	// Flags only, the next GhostFrame taking the linkage.
	for (unsigned int i = 0; i < count; ++i)
		ghost_HideLegacy(pIds[i], NULL, false);
}

//*****************************************************************************
//	LOCAL ANIMATION
//
// Each mirror runs its own state machine between records, with no action
// functions but the spawners, the wire resyncing it.

// The highest render tier running the local state machine: 0 off, 1 NEAR,
// 2 NEAR and MID, 3 all.
CVAR(Int, fua_csnap_localanim, 3, CVAR_NOSETBYACS)

// The loop-hint census, off by default, as it walks a shadow state machine.
CVAR(Bool, fua_csnap_loopcensus, false, CVAR_NOSETBYACS)

// Whether a mirror runs the state machine, which events are gated on too.
static bool ghost_MirrorAnimates(unsigned int id)
{
	if ((g_mirrorTier == NULL) || (id >= SNAP_MAXID))
		return false;

	return ((int)g_mirrorTier[id] < fua_csnap_localanim);
}

// How long the wire may be quiet before a disagreeing mirror takes its frame.
#define GHOST_ANIM_RESYNC_MS	1000



// "TROO C" rather than a packed integer, so a client reading and a server
// reading can be compared by eye.
static void ghost_DescribeFrame(WORD sprite, BYTE frame, char* pszOut, size_t len)
{
	const char* pszName = (sprite < sprites.Size()) ? sprites[sprite].name : "????";
	mysnprintf(pszOut, len, "%.4s %c", pszName, (char)('A' + (frame & 31)));
}

static bool ghost_StateMatches(const FState* pState, WORD wantSprite, BYTE wantFrame)
{
	if (pState->GetFrame() != (int)wantFrame)
		return false;

	// SPR_NOCHANGE and SPR_FIXED states inherit the actor's current sprite, so
	// they match on frame alone.
	return (pState->sprite == (int)wantSprite) ||
		(pState->sprite == SPR_NOCHANGE) ||
		(pState->sprite == SPR_FIXED);
}

// Records resolved by state this window, which gates the event and missile
// report block.
static unsigned int g_stateRecs = 0;

// Prints a census by tier, each percentage beside its count.
static void ghost_PrintTierSplit(const char* pszWhat, const unsigned int* pOk,
	const unsigned int* pMiss)
{
	double pct[3];
	unsigned int tot[3];
	for (unsigned int t = 0; t < 3; ++t)
	{
		tot[t] = pOk[t] + pMiss[t];
		pct[t] = (tot[t] > 0) ? (100.0 * (double)pOk[t] / (double)tot[t]) : 0.0;
	}
	Printf("[snap] %s by tier: near %.0f%% (%u) | mid %.0f%% (%u) | far %.0f%% (%u)\n",
		pszWhat, pct[0], tot[0], pct[1], tot[1], pct[2], tot[2]);
}

// bystate counts samples and agreements rather than ok/miss; convert.
static void ghost_PrintTierAgree(const char* pszWhat, const unsigned int* pAgree,
	const unsigned int* pSamp)
{
	unsigned int miss[3];
	for (unsigned int t = 0; t < 3; ++t)
		miss[t] = (pSamp[t] >= pAgree[t]) ? (pSamp[t] - pAgree[t]) : 0;
	ghost_PrintTierSplit(pszWhat, pAgree, miss);
}

// The same census by state identity rather than the drawn frame, which
// monsters share across states.
unsigned int g_stAgree = 0;
unsigned int g_stTierAgree[3] = { 0, 0, 0 };
unsigned int g_stTierSamp[3] = { 0, 0, 0 };
unsigned int g_stBehind[5] = { 0,0,0,0,0 };
unsigned int g_stAhead = 0;
unsigned int g_stOffChain = 0;
unsigned int g_stNoIdx = 0;

// The control-group split [exempt][tier], telling suppression's selection from
// its damage within one run.
unsigned int g_stSplitAgree[2][3] = { { 0,0,0 }, { 0,0,0 } };
unsigned int g_stSplitSamp[2][3] = { { 0,0,0 }, { 0,0,0 } };

// The mirror against the state the wire named, within four steps or diverged,
// by tics animated unaided (read with suppression off).
#define GST_GAPS 5
static const int g_stGapMax[GST_GAPS] = { 2, 5, 10, 20, 0x7FFFFFFF };
static unsigned int g_stGapSamp[GST_GAPS] = { 0,0,0,0,0 };
static unsigned int g_stGapAgree[GST_GAPS] = { 0,0,0,0,0 };

static void ghost_CensusGap(const AActor * pActor, FState * pWire, int gapTics)
{
	if ((pWire == NULL) || (gapTics <= 0))
		return;
	int b = 0;
	while ((b < GST_GAPS - 1) && (gapTics > g_stGapMax[b]))
		++b;
	++g_stGapSamp[b];
	if (pActor->state == pWire)
		++g_stGapAgree[b];
}

static void ghost_CensusByState(const AActor * pActor, FState * pWire, BYTE tier,
	unsigned int id)
{
	if (pWire == NULL) { ++g_stNoIdx; return; }
	const BYTE t = (tier < 3) ? tier : 2;

	// Control-group split, before the early return below so both columns count
	// the agreeing case.
	{
		const unsigned int ex = AppearExempt((WORD)id) ? 1u : 0u;
		++g_stSplitSamp[ex][t];
		if (pActor->state == pWire)
			++g_stSplitAgree[ex][t];
	}

	++g_stTierSamp[t];
	if (pActor->state == pWire) { ++g_stAgree; ++g_stTierAgree[t]; return; }
	const FState * pWalk = pActor->state;
	for (int step = 0; (step < 4) && (pWalk != NULL); ++step)
	{
		pWalk = pWalk->GetNextState();
		if (pWalk == pWire) { ++g_stBehind[step]; return; }
	}

	// And the other direction, bounded the same way, so a mirror that ran a
	// step or two past the wire is not counted as a full divergence.
	pWalk = pWire;
	for (int step = 0; (step < 4) && (pWalk != NULL); ++step)
	{
		pWalk = pWalk->GetNextState();
		if (pWalk == pActor->state) { ++g_stAhead; return; }
	}
	++g_stOffChain;
}



// The state the wire names through the server's resolver, or NULL, a psprite's
// against the weapon's class.
static FState * ghost_StateFromType(const PClass * pType, WORD idx)
{
	return StateFromQState(pType, idx);
}

static FState * ghost_StateFromIndex(const AActor * pActor, WORD idx)
{
	return StateFromQState(pActor->GetClass(), idx);
}

// Applies a psprite record to its remote player, writing the layers directly
// so the weapon draws and never fires.

static unsigned int g_psprApply = 0, g_psprBadClass = 0, g_psprBadState = 0;

static void ghost_ApplyPSprite(unsigned int id, const SnapPSpriteState & st)
{
	if ((id == 0) || (id > (unsigned int)MAXPLAYERS))
		return;

	// Invert the reserved id through the same rule that made it, rather than
	// assuming the arithmetic (3.1).
	unsigned int pl = MAXPLAYERS;
	for (unsigned int p = 0; p < (unsigned int)MAXPLAYERS; ++p)
	{
		if ((unsigned int)IDList<AActor>::reservedIDForPlayer((int)p) == id)
		{
			pl = p;
			break;
		}
	}
	if (pl >= (unsigned int)MAXPLAYERS)
		return;

	// The local player's psprites are predicted, never overwritten (4.2).
	if ((int)pl == consoleplayer)
		return;
	if ((playeringame[pl] == false) || (players[pl].mo == NULL))
		return;

	// weaponClass 0 is no weapon, which blanks both layers.
	if (st.weaponClass == 0)
	{
		players[pl].psprites[ps_weapon].state = NULL;
		players[pl].psprites[ps_flash].state = NULL;
		++g_psprApply;
		return;
	}

	const PClass * const pType =
		NETWORK_GetClassFromIdentification((unsigned short)st.weaponClass);
	if (pType == NULL)
	{
		++g_psprBadClass;
		return;
	}

	for (unsigned int L = 0; L < 2; ++L)
	{
		pspdef_t & psp = players[pl].psprites[(L == 0) ? ps_weapon : ps_flash];
		if (st.qstate[L] == SNAP_STATE_UNKNOWN)
		{
			// Genuinely unset - an empty flash layer is the common case.
			psp.state = NULL;
			continue;
		}
		FState * const pState = ghost_StateFromType(pType, st.qstate[L]);
		if (pState == NULL)
		{
			// A state this client cannot resolve leaves the layer alone.
			++g_psprBadState;
			continue;
		}
		psp.state = pState;
		psp.tics = (st.qtics[L] == 255) ? -1 : (int)st.qtics[L];
	}
	++g_psprApply;
}

// The state with the wire's sprite and frame, searched in the mirror's own
// sequence first, since monsters reuse frames across sequences.
static FState* ghost_FindState(const AActor* pActor, WORD wantSprite, BYTE wantFrame, int* outSteps = NULL)
{
	if (outSteps != NULL)
		*outSteps = -1;

	const PClass* pType = pActor->GetClass();
	if ((pType == NULL) || (pType->ActorInfo == NULL))
		return NULL;

	// Forward from where we are, far enough to cross any plausible animation
	// loop but bounded, since these chains are usually cyclic.
	const FState* pStart = pActor->state;
	FState* pWalk = (pStart != NULL) ? pStart->GetNextState() : NULL;
	for (int step = 0; (step < 16) && (pWalk != NULL); ++step)
	{
		if (ghost_StateMatches(pWalk, wantSprite, wantFrame))
		{
			if (outSteps != NULL)
				*outSteps = step + 1;

			return pWalk;
		}
		if (pWalk == pStart)
			break;			// closed the loop; the frame is not in this one
		pWalk = pWalk->GetNextState();
	}

	FState* pStates = pType->ActorInfo->OwnedStates;
	const int count = pType->ActorInfo->NumOwnedStates;
	if ((pStates == NULL) || (count <= 0))
		return NULL;

	for (int i = 0; i < count; ++i)
	{
		if (ghost_StateMatches(&pStates[i], wantSprite, wantFrame))
			return &pStates[i];
	}
	return NULL;
}

// Whether a state's zero-duration chain dead-ends, where SetState would destroy
// the mirror, which instead holds its sprite and frame.
static bool ghost_StateChainDeadEnds(AActor* pActor, FState* pState)
{
	// SetState's walk, with its scaled AActor::GetTics.
	int guard = 0;
	while (pState != NULL)
	{
		if (pActor->GetTics(pState) != 0)
			return false;					// lands on a real, timed state
		if (++guard > 64)
			return false;					// pathological loop; treat as safe
		pState = pState->GetNextState();
	}
	return true;						// reached NULL - SetState would destroy
}

// Whether either state reaches the other along its chain within maxSteps.
static bool ghost_SameChain(FState * pA, FState * pB, int maxSteps = 8)
{
	for (int dir = 0; dir < 2; ++dir)
	{
		const FState * pWalk = (dir == 0) ? pA : pB;
		const FState * const pWant = (dir == 0) ? pB : pA;
		for (int step = 0; (step < maxSteps) && (pWalk != NULL); ++step)
		{
			pWalk = pWalk->GetNextState();
			if (pWalk == pWant)
				return true;
		}
	}
	return false;
}

// Whether a mirror rests in a final state the wire's state reaches, ahead of
// the wire rather than wrong.
static bool ghost_RestsAhead(const AActor * pActor, const FState * pWire)
{
	if ((pActor->tics >= 0) || (pWire == NULL) || (pActor->state == NULL)
		|| (pActor->sprite != pActor->state->sprite)
		|| (pActor->frame != pActor->state->GetFrame()))
		return false;
	const FState * pWalk = pWire;
	for (int step = 0; (step < 64) && (pWalk != NULL); ++step)
	{
		pWalk = pWalk->GetNextState();
		if (pWalk == pActor->state)
			return true;
	}
	return false;
}

// Advances a mirror's animation, with a ring as the per-frame advance.
static void ghost_AdvanceAnim(AActor * pActor, int tics, MsRing * pRing = NULL);

// Whether this line stops a body of the class defaults touching it at (sx, sy),
// by PIT_CheckLine's flags and P_TryMove's plane openings.
static bool ghost_LineBlocks(const SnapBodyFlags & body, bool bProjectile, const line_t * ld,
	fixed_t sx, fixed_t sy, fixed_t refx, fixed_t refy, double z)
{
	if (ld->backsector == NULL)
		return true;
	SnapLineFlags lf;
	lf.bBlockEverything = (ld->flags & ML_BLOCKEVERYTHING) != 0;
	lf.bBlockProjectile = (ld->flags & ML_BLOCKPROJECTILE) != 0;
	lf.bRailing = (ld->flags & ML_RAILING) != 0;
	lf.bBlocking = (ld->flags & ML_BLOCKING) != 0;
	lf.bBlockMonsters = (ld->flags & ML_BLOCKMONSTERS) != 0;
	lf.bBlockFloaters = (ld->flags & ML_BLOCK_FLOATERS) != 0;
	bool bRail = false;
	if (ComputeLineFlagsBlock(lf, body, bProjectile, bRail))
		return true;
	FLineOpening open;
	P_LineOpening(open, NULL, ld, sx, sy, refx, refy);
	double bottom = (double)open.bottom;
	if (bRail)
	{
		// 32 up, or with the hack only from the high side (its own floor).
		const sector_t * const pSec = P_PointInSector(refx, refy);
		if (((level.flags2 & LEVEL2_RAILINGHACK) == 0)
			|| ((pSec != NULL) && (open.bottom == pSec->floorplane.ZatPoint(sx, sy))))
			bottom += 32.0 * (double)FRACUNIT;	// [Kaori-Chani] double arithmetic, as before
	}
	return ComputeOpeningBlocks(body, (double)open.top, bottom, z);
}

// How far an extrapolated ghost goes before its box touches a wall, negative
// if nothing blocks within its cap (cl_ghostactor.md).
static float ghost_WallStop(const AActor * pActor, const SnapGhostPose & pose)
{
	const double F = 65536.0;
	const AActor * const def = pActor->GetDefault();
	SnapWallPath path;
	if (ComputeWallPath(pose.vx, pose.vy, pose.exCapMS, def->radius, pose.ox, pose.oy, path)
		== false)
		return -1.0f;
	if ((def->flags & MF_NOCLIP) || (def->flags5 & MF5_NOINTERACTION))
		return -1.0f;
	// A projectile by default or by the wire's bits, except a monster.
	const bool bProjectile = ComputeWallProjectile((def->flags & MF_MISSILE) != 0,
		(def->BounceFlags & BOUNCE_MBF) != 0, (def->flags3 & MF3_ISMONSTER) != 0, pose.misc);
	SnapBodyFlags body;
	body.bNoBlockMonst = (def->flags3 & MF3_NOBLOCKMONST) != 0;
	body.bFloat = (def->flags & MF_FLOAT) != 0;
	body.bCeilingHugger = (def->flags3 & MF3_CEILINGHUGGER) != 0;
	body.bFloorHugger = (def->flags3 & MF3_FLOORHUGGER) != 0;
	body.bMissile = (def->flags & MF_MISSILE) != 0;
	body.bStepMissile = (def->flags6 & MF6_STEPMISSILE) != 0;
	body.height = (double)def->height;
	body.maxStep = (double)def->MaxStepHeight;

	++g_wallTraces;
	FPathTraverse it(pose.ox, pose.oy, pose.ox + (fixed_t)(path.dx * path.traceLen * F),
		pose.oy + (fixed_t)(path.dy * path.traceLen * F), PT_ADDLINES);
	double best = -1.0;
	intercept_t * in;
	while ((in = it.Next()) != NULL)
	{
		if (in->isaline == false)
			continue;
		const line_t * const ld = in->d.line;
		SnapWallContact c;
		if (ComputeWallContact(path, (double)ld->v1->x / F, (double)ld->v1->y / F,
			(double)ld->dx / F, (double)ld->dy / F, (double)in->frac / 65536.0, best,
			(double)pose.oz, (double)pose.vz, pose.exAccelZ, c) == false)
			continue;
		if (ghost_LineBlocks(body, bProjectile, ld, (fixed_t)(c.px * F), (fixed_t)(c.py * F),
			(fixed_t)(c.cx * F), (fixed_t)(c.cy * F), c.z))
			best = c.s;
	}
	return (float)best;
}

// [Kaori-Chani] The settings every one of the three below reads, so none of them can
// disagree about what the smoothing was configured to do.
static SnapSmoothCfg ghost_SmoothCfg(unsigned int id)
{
	SnapSmoothCfg cfg;
	cfg.stepLimit = fua_csnap_smooth_step;
	cfg.extrapLimit = fua_csnap_smooth_nonballistic;
	cfg.missileLimit = fua_csnap_smooth_ballistic;
	cfg.bStepPop = ComputeSmoothStepPop(fua_csnap_smooth_entities, id, consoleplayer, MAXPLAYERS);
	cfg.bInReset = g_smoothInReset;
	cfg.ticRate = TICRATE;
	return cfg;
}

// [Kaori-Chani] The fold, and the only thing that takes a mirror's smoothing to write it:
// a packet's records fold in at the moment it arrived, and that state is what carries
// forward, so a second packet in the same tic starts from the first.
static void ghost_Fold(SnapSmooth & s, unsigned int id, const SnapGhostPose & pose,
	unsigned int nowMS, SnapSmoothStats & st, fixed_t & outX, fixed_t & outY, fixed_t & outZ)
{
	ComputeSmooth(s, ComputeSmoothIn(pose), ghost_SmoothCfg(id), nowMS, st, outX, outY, outZ);
}

// [Kaori-Chani] A fold into a copy the caller owns, so the frame can fold at its own
// moment and then on to the tic's end from that same state, as the replay does.
static void ghost_FoldCopy(SnapSmooth & copy, unsigned int id, const SnapGhostPose & pose,
	unsigned int atMS, fixed_t & outX, fixed_t & outY, fixed_t & outZ)
{
	// A scratch census: the counts that are the tic's are the carried fold's alone.
	SnapSmoothStats scratch;
	memset(&scratch, 0, sizeof(scratch));
	ComputeSmooth(copy, ComputeSmoothIn(pose), ghost_SmoothCfg(id), atMS, scratch,
		outX, outY, outZ);
}

// [Kaori-Chani] features/net-snapshot: the fold, on the packet clock. Every id this
// payload wrote a record for folds into its own carried smoothing at the moment the
// payload arrived, so two packets inside one tic fold in turn rather than both from the
// tic's end, and the frame draws where the newest one left the mirror.
void zx::netsnapshot::GhostFoldApplied(unsigned int nowMS)
{
	if (g_smooth == NULL)
		return;

	unsigned int n = 0;
	const unsigned int * pIds = TouchedIds(n);
	if ((pIds == NULL) || (n == 0))
		return;

	// The tic before this one is counted now, so what is held is always the newest
	// refill of the tic in hand.
	if (gametic != g_smTicCounted)
	{
		ComputeCensusAdd(g_sm, g_smTic, false);
		g_smTicCounted = gametic;
	}
	SnapSmoothStats thisRefill;
	memset(&thisRefill, 0, sizeof(thisRefill));

	for (unsigned int k = 0; k < n; ++k)
	{
		const unsigned int id = pIds[k];
		if (id >= SNAP_MAXID)
			continue;

		// Quietly: the drawn jump is the tic-end sample's to measure, not this one's.
		SnapGhostPose pose;
		if (SampleGhostQuiet(id, nowMS, pose) == false)
			continue;

		fixed_t hx = pose.x, hy = pose.y, hz = pose.z;
		ghost_Fold(g_smooth[id], id, pose, nowMS, thisRefill, hx, hy, hz);
	}

	g_smTic = thisRefill;
}

// [Kaori-Chani] Whether the renderer slides this one, read from the folded state.
static bool ghost_Slides(const SnapSmooth & s, unsigned int id, const SnapGhostPose & pose,
	bool bRawWalk)
{
	return ComputeSmoothSlides(s, ComputeSmoothIn(pose), ghost_SmoothCfg(id), bRawWalk);
}

// Sets the mirror's state with the wire's remaining tics (255 unknown), aged
// forward by `ageMS`, holding where the chain dead-ends.
static void ghost_SetStateSafe(AActor* pActor, FState* pState, WORD wantSprite, BYTE wantFrame, BYTE wireTics = 255, unsigned int ageTics = 0)
{
	if ((pState != NULL) && (ghost_StateChainDeadEnds(pActor, pState) == false))
	{
		// A jump's state only; the aging below never spawns.
		if (g_msSetOwed >= 0)
			ms_SetStateSpawning(pActor, pState, (unsigned int)g_msSetOwed);
		else
			pActor->SetState(pState, true);

		if ((wireTics != 255) && (pActor->tics > 0))
		{
			pActor->tics = (wireTics > 0) ? (int)wireTics : 1;
			// The age arrives in TICS and is used as tics, with no
			// conversion.
			if (ageTics > 0)
				ghost_AdvanceAnim(pActor, (int)ageTics);
		}
	}
	else
	{
		// Would dead-end into destruction; show the frame verbatim and hold.
		pActor->sprite = wantSprite;
		pActor->frame = wantFrame;
		pActor->tics = -1;
	}
}


// LOOP HINT CENSUS
//
// Whether a shadow state machine, never drawn, reaches the server's next state
// unaided, [gap bucket][exact, behind, ahead, off-chain].
#define GHOST_LC_BUCKETS	SNAP_SHADOW_BUCKETS
#define GHOST_LC_OUTCOMES	SNAP_SHADOW_OUTCOMES

static unsigned int g_lcBucket[GHOST_LC_BUCKETS][GHOST_LC_OUTCOMES] = { { 0 } };
static unsigned int g_lcNoIndex = 0;	// wire state unresolvable

static const char* const g_szLcBucket[GHOST_LC_BUCKETS] =
{ "<100ms", "<250", "<500", "<1s", "1s+" };

// Walks the shadow, skipping zero-duration states as SetState does, with no
// actor or action.
static void ghost_ShadowAdvance(GhostShadow & sh, int tics)
{
	for (int i = 0; i < tics; ++i)
	{
		if ((sh.state == NULL) || (sh.tics <= 0))
			return;
		if (--sh.tics > 0)
			continue;

		FState * pNext = sh.state->GetNextState();
		unsigned int guard = 0;
		while ((pNext != NULL) && (pNext->GetTics() == 0) && (guard++ < 64))
			pNext = pNext->GetNextState();
		if ((pNext == NULL) || (guard >= 64))
		{
			sh.tics = -1;			// terminal: hold, as the mirror would
			return;
		}
		sh.state = pNext;
		sh.tics = pNext->GetTics();
	}
}

// A state's default successor, for the census's walks.
static FState* ghost_NextState(FState* pState)
{
	return pState->GetNextState();
}

// Scores the shadow against the state the wire just named, in
// server tics, then resyncs it.
static void ghost_ShadowScore(unsigned int id, FState * pWire, int wireTics, int gapTics)
{
	if (g_shadow == NULL)
		return;
	GhostShadow & sh = g_shadow[id];

	if (pWire == NULL)
	{
		++g_lcNoIndex;
		sh.valid = false;
		return;
	}

	// Bucket by the entity's own update gap (ComputeShadowBucket).
	const unsigned int b = ComputeShadowBucket(gapTics);

	if (sh.valid == false)
	{
		sh.state = pWire;
		sh.tics = wireTics;
		sh.valid = true;
		return;					// first sighting: nothing to predict from
	}

	// From the previous record by the server tics between the captures.
	ghost_ShadowAdvance(sh, gapTics);

	// Exact, behind, ahead or off-chain, each walk bounded (ComputeShadowVerdict).
	++g_lcBucket[b][ComputeShadowVerdict(sh.state, pWire, ghost_NextState)];

	sh.state = pWire;
	sh.tics = wireTics;
}

static void ghost_AdvanceAnim(AActor* pActor, int tics, MsRing* pRing)
{

	for (int i = 0; i < tics; ++i)
	{
		// Negative tics is an infinite-duration state; zero should not occur,
		// because SetState loops past zero-duration states itself.
		if (pActor->tics <= 0)
			return;

		if (--pActor->tics > 0)
			continue;

		FState* pNext = (pActor->state != NULL) ? pActor->state->GetNextState() : NULL;
		if (pNext == NULL)
		{
			// The sequence's end, where a mirror holds its last frame.
			pActor->tics = -1;
			return;
		}

		// A chain that dead-ends holds the current frame.
		if (ghost_StateChainDeadEnds(pActor, pNext))
		{
			pActor->tics = -1;
			return;
		}

		if (pRing == NULL)
		{
			if (pActor->SetState(pNext, true) == false)
				return;
			continue;
		}
		// Entered at this step of the advance, so owed the steps after it.
		const unsigned int owed = (unsigned int)(tics - 1 - i);
		if (ms_SetStateSpawning(pActor, pNext, owed) == false)
			return;
		ms_PushChain(*pRing, pNext, pActor->state, gametic - (int)owed);
	}
}

// A record sets the mirror's state, a jump running its spawners.
static void ms_RecordSet(MsRing* pRing, AActor* pActor, FState* pState, bool bNamed, bool bHard,
	WORD wantSprite, BYTE wantFrame, BYTE wireTics, unsigned int ageTics)
{
	if (pRing == NULL)
	{
		ghost_SetStateSafe(pActor, pState, wantSprite, wantFrame, wireTics, ageTics);
		return;
	}
	const int recTic = gametic - (int)ageTics;
	int verdict = MS_SEED;
	if (bHard)
		++g_msHard;
	else if ((bNamed == false) || (pState == NULL) || (pActor->state == NULL))
		++g_msUnnamed;
	else
	{
		verdict = ms_Classify(*pRing, pActor, pState, recTic, (int)ageTics);
		if (verdict == MS_SEED)
			++g_msSeed;
	}
	switch (verdict)
	{
	case MS_SEED:		break;
	case MS_RESYNC:		++g_msResync;		break;
	case MS_BEHIND:		++g_msBehind;		break;
	case MS_UNDECIDED:	++g_msUndecided;	break;
	case MS_JUMP:		++g_msJump;			break;
	}
	g_msSetOwed = (verdict == MS_JUMP) ? (int)MIN<unsigned int>(ageTics, MS_MAXOWED) : -1;
	ghost_SetStateSafe(pActor, pState, wantSprite, wantFrame, wireTics, ageTics);
	g_msSetOwed = -1;
	pRing->bSeed = false;
	if (pState != NULL)
		ms_PushChain(*pRing, pState, NULL, recTic);
	if (pActor->state != NULL)
		ComputeMsPush(*pRing, pActor->state, gametic);
}

//*****************************************************************************
//	PER-FRAME SYNC

// The render cull's per-frame census by tier.
static QWORD g_ghostNearAccum = 0, g_ghostMidAccum = 0, g_ghostFarAccum = 0;
static QWORD g_ghostRelinkAccum = 0, g_ghostCreateAccum = 0, g_ghostRecreateAccum = 0;
// GC::AllocBytes across the window, the collector's pressure.
static size_t g_gcAllocMin = 0, g_gcAllocMax = 0;
static bool g_gcAllocSeen = false;
// GhostFrame's own time, in raw monotonic counts.
static QWORD g_ghostSelfRaw = 0;
// Frame phases from d_main.cpp's loop: sim (TryRunTics) and render (D_Display).
unsigned long long g_frameSimRaw = 0;
unsigned long long g_frameRenderRaw = 0;
unsigned long long g_frameTicsRun = 0;		// tics executed across the window
unsigned long long g_ticThinkersRaw = 0;		// RunThinkers time across the window
unsigned long long g_ticEffectsRaw = 0;		// P_RunEffects time across the window
unsigned long long g_applyDeltaRaw = 0;		// ApplyDelta (snapshot parse) time
unsigned long long g_gtickerRaw = 0;			// G_Ticker time across the window
unsigned long long g_netupdateRaw = 0;		// NetUpdate time across the window
extern unsigned int I_FPSTime();

// Raw counts -> milliseconds, at microsecond resolution so a sub-ms phase is
// visible rather than truncated to zero.
static double ghost_RawToMS(QWORD raw)
{
	return (double)(QWORD)TIC_ScaleRaw((tictime_raw_t)raw, 1000000) / 1000.0;
}

static unsigned int g_ghostRelinkMax = 0;
static QWORD g_ghostSolidAccum = 0;
static QWORD g_ghostSolidChangeAccum = 0;
static unsigned int g_ghostSolidMax = 0;
static QWORD g_ghostSolidCorpseAccum = 0;
static unsigned int g_ghostCensusFrames = 0;
static unsigned int g_ghostNearMax = 0;
static unsigned int g_ghostReportMS = 0;	// [Kaori-Chani] I_MSTime's width, so it wraps with it

static void ghost_Report();

void zx::netsnapshot::GhostFrame()
{
	// The hooks, here as this runs before any ghost is sampled.
	static bool s_hooksInstalled = false;
	if (s_hooksInstalled == false)
	{
		SetLawGravity(LawAt);
		SetAnimatesLookup(ghost_MirrorAnimates);
		SetPSpriteSink(ghost_ApplyPSprite);
		SetPropsSink(ghost_ApplyProps);
		s_hooksInstalled = true;
	}

	// Every frame, ahead of the gate, as consoleplayer changes on a reconnect.
	SetLocalPlayerId(IDList<AActor>::reservedIDForPlayer(consoleplayer));

	const tictime_raw_t ghostStartRaw = TIC_GetRawCounter();
	unsigned int nNear = 0, nMid = 0, nFar = 0;	// this frame's per-tier counts
	unsigned int nRelink = 0, nCreate = 0;		// SetOrigin + CreateMirror this frame
	unsigned int nRecreate = 0;					// diag: creates that were recreates

	// sample GC allocation this frame for the pressure probe.
	{
		const size_t a = GC::AllocBytes;
		if (!g_gcAllocSeen) { g_gcAllocMin = g_gcAllocMax = a; g_gcAllocSeen = true; }
		else { if (a < g_gcAllocMin) g_gcAllocMin = a; if (a > g_gcAllocMax) g_gcAllocMax = a; }
	}

	if (fua_csnap_ghosts == false)
	{
		if (g_mirrorCount != 0)
			GhostClearAll();
		return;
	}

	ghost_Init();

	// Everything is sampled at now on the deltas' clock, once a frame.
	const unsigned int renderMS = I_MSTime();

	// [Kaori-Chani] The tic's end on that clock, and this frame's fraction of it: r_TicFrac
	// is set after the fill, so reading it here would give the frame before.
	uint32 ticEnd32 = 0;
	const fixed_t ticFracNow = I_GetTimeFrac(&ticEnd32);
	const fixed_t ticFrac = (cl_capfps || r_NoInterpolate) ? (fixed_t)FRACUNIT : ticFracNow;
	const unsigned int tickEndMS = (unsigned int)ticEnd32;

	// Smoothing restarts with the stream and around a map reset.
	if (g_smooth != NULL)
	{
		const bool bInReset = MoverInReset();
		if ((StreamEpoch() != g_smoothEpoch) || (bInReset != g_smoothInReset))
		{
			memset(g_smooth, 0, SNAP_MAXID * sizeof(SnapSmooth));
			g_smoothEpoch = StreamEpoch();
		}
		g_smoothInReset = bInReset;
	}

	// A mirror never ticks, so its slide ends here at each new tic.
	if (gametic != g_slideFrameTic)
	{
		g_slideFrameTic = gametic;
		for (unsigned int k = 0; k < g_mirrorIds.Size(); ++k)
		{
			AActor * const pM = g_mirror[g_mirrorIds[k]];
			if (pM == NULL)
				continue;
			pM->PrevX = pM->x;
			pM->PrevY = pM->y;
			pM->PrevZ = pM->z;
		}
	}

	// Local animation runs on the tic clock, per mirror, as a far one is
	// visited one frame in eight.

	// No age correction, which would compute zero at the tic rate.

	// The ghost table's compact present list and our own compact list of
	// mirrors, so the cost follows the population, not the id space.
	unsigned int count = 0;
	const unsigned int* pIds = PresentIds(count);

	// The camera for the render cull, which is off without one.
	const AActor* pCam = NULL;
	if ((consoleplayer >= 0) && (consoleplayer < MAXPLAYERS))
	{
		pCam = players[consoleplayer].camera;
		if (pCam == NULL)
			pCam = players[consoleplayer].mo;
	}
	const bool cullActive = fua_csnap_rendercull && (pCam != NULL);

	// Collision measures from the player's body, not the camera.
	const AActor* pSelf = ((consoleplayer >= 0) && (consoleplayer < MAXPLAYERS))
		? players[consoleplayer].mo : NULL;
	const bool collideActive = fua_csnap_collide && (pSelf != NULL);
	const long long collideIn = (long long)fua_csnap_collide_dist * (long long)fua_csnap_collide_dist;
	// Hysteresis: 25% further out to drop it again, or a mirror sitting on the
	// boundary relinks twice a frame forever.
	const long long collideOut = (collideIn * 25) / 16;
	unsigned int nSolid = 0, nSolidChange = 0, nSolidCorpse = 0;

	// Camera for the missile census, which splits capped samples by distance.
		if (pCam != NULL)
		SetViewPoint(pCam->x, pCam->y);

	// The recorder's frame marker: the render clock and pCam's
	// pose, before any sampling.
	if (RecorderActive())
	{
		CsnapPlayerPose recPose;
		memset(&recPose, 0, sizeof(recPose));
		if (pCam != NULL)
		{
			recPose.x = pCam->x;
			recPose.y = pCam->y;
			recPose.z = pCam->z;
			recPose.angle = pCam->angle;
			recPose.pitch = (int)pCam->pitch;
			recPose.valid = true;
		}
		RecorderFrame(renderMS, recPose);
	}

	// Squared thresholds with the hysteresis band folded in.
	const long long nearIn  = (long long)(fua_csnap_cull_near - GHOST_CULL_HYST) * (fua_csnap_cull_near - GHOST_CULL_HYST);
	const long long nearOut = (long long)(fua_csnap_cull_near + GHOST_CULL_HYST) * (fua_csnap_cull_near + GHOST_CULL_HYST);
	const long long farIn   = (long long)(fua_csnap_cull_far  - GHOST_CULL_HYST) * (fua_csnap_cull_far  - GHOST_CULL_HYST);
	const long long farOut  = (long long)(fua_csnap_cull_far  + GHOST_CULL_HYST) * (fua_csnap_cull_far  + GHOST_CULL_HYST);

	// One rotating slice of FAR ghosts is refreshed per tic, which at distance is
	// invisible.
	// [Kaori-Chani] Two phases off the tic, not eight off the call: a tic of two packets
	// advanced it twice, and eight would refresh a far mirror far slower (README).
	g_cullRotor = (unsigned int)(gametic & 1);

	g_playerGhostSeen = g_playerGhostExcluded = 0;

	for (unsigned int i = 0; i < count; ++i)
	{
		const unsigned int id = pIds[i];

		SnapGhostPose pose;
		if (SampleGhost(id, tickEndMS, pose) == false)
			continue;

		// An excluded player gets no mirror, which would double it.
		if (id <= MAXPLAYERS)
		{
			++g_playerGhostSeen;
			if (ghost_ExcludedPlayer(id))
			{
				++g_playerGhostExcluded;

				// A mirror an excluded player still has is cleared here.
				if (g_mirror[id] != NULL)
				{
					ghost_ShowLegacy(id, true);
					g_mirror[id]->ObjectFlags &= ~OF_Fixed;
					g_mirror[id]->Destroy();
					g_mirror[id] = NULL;
					ms_Forget(id);
					if (g_mirrorCount > 0)
						--g_mirrorCount;
				}
				continue;
			}
		}

		AActor* pActor = g_mirror[id];
		const bool bCreated = (pActor == NULL);
		if (pActor == NULL)
		{
			// diag: was this a RECREATE (had a mirror very recently -> the
			// destroy/recreate thrash) or a genuinely new ghost?
			if (g_hadMirror != NULL && g_hadMirror[id])
				++nRecreate;
			pActor = ghost_CreateMirror(id, GhostClassId(id), pose.x, pose.y, pose.z);
			++nCreate;
			if (pActor == NULL)
				continue;
		}

		// The shooter as target, its mirror first, until one resolves, a
		// changed shooter dropping the old target.
		{
			bool bResolved = false, bChanged = false;
			const unsigned short tid = GhostTarget(id, bResolved, bChanged);
			if (bChanged)
				pActor->target = NULL;
			if ((tid != 0) && (bResolved == false))
			{
				AActor* pTarget = MirrorForNetID(tid, false);
				if (pTarget == NULL)
					pTarget = CLIENT_FindThingByNetID(tid);
				if (pTarget != NULL)
				{
					pActor->target = pTarget;
					GhostTargetResolved(id);
					if (bCreated)
						++g_tgtAtEnter;
					else
						++g_tgtLate;
				}
			}
		}

		// Camera distance for this mirror, for the tier logic.
		long long camDistSq = -1;
		if (pCam != NULL)
		{
			const long long cdx = (long long)(pose.x - pCam->x) >> FRACBITS;
			const long long cdy = (long long)(pose.y - pCam->y) >> FRACBITS;
			camDistSq = cdx * cdx + cdy * cdy;
		}

		// Solidity before the tier logic, both directions unlinking first.
		if (collideActive)
		{
			const bool isSolid = ((pActor->flags & MF_NOBLOCKMAP) == 0);
			const long long sdx = (long long)(pose.x - pSelf->x) >> FRACBITS;
			const long long sdy = (long long)(pose.y - pSelf->y) >> FRACBITS;
			const long long sdsq = sdx * sdx + sdy * sdy;
			// A corpse, by the wire bit, is never solid.
			const bool corpse = ((pose.misc & SNAPMISC_CORPSE) != 0);
			const bool want = (corpse == false)
				&& (isSolid ? (sdsq <= collideOut)
					: ((sdsq <= collideIn) && ghost_ClassCollides(GhostClassId(id))));
			if (corpse && isSolid)
				++nSolidCorpse;

			if (want != isSolid)
			{
				pActor->UnlinkFromWorld();
				if (want)
					pActor->flags &= ~(MF_NOBLOCKMAP | MF_NOCLIP);
				else
					pActor->flags |= MF_NOBLOCKMAP | MF_NOCLIP;
				pActor->LinkToWorld();
				++nSolidChange;
			}
			if (want)
				++nSolid;
		}
		else if ((pActor->flags & MF_NOBLOCKMAP) == 0)
		{
			// Turned off mid-session: put back what we cleared.
			pActor->UnlinkFromWorld();
			pActor->flags |= MF_NOBLOCKMAP | MF_NOCLIP;
			pActor->LinkToWorld();
			++nSolidChange;
		}

		// This ghost's work tier by distance to the camera, with hysteresis.
		BYTE tier = GTIER_NEAR;
		bool promotedToNear = false;
		bool bWasAnimating = true;
		if (cullActive)
		{
			const long long dsq = camDistSq;

			const BYTE prev = g_mirrorTier[id];
			tier = prev;
			bWasAnimating = ((int)prev < fua_csnap_localanim);
			// Promote toward NEAR at inner edges, demote toward FAR at outer.
			if (prev == GTIER_NEAR)
			{
				if (dsq > farOut) tier = GTIER_FAR;
				else if (dsq > nearOut) tier = GTIER_MID;
			}
			else if (prev == GTIER_MID)
			{
				if (dsq < nearIn) tier = GTIER_NEAR;
				else if (dsq > farOut) tier = GTIER_FAR;
			}
			else // GTIER_FAR
			{
				if (dsq < nearIn) tier = GTIER_NEAR;
				else if (dsq < farIn) tier = GTIER_MID;
			}
			// A promotion to NEAR resyncs the state a non-animating tier left.
			promotedToNear = (prev != GTIER_NEAR) && (tier == GTIER_NEAR);
			g_mirrorTier[id] = tier;

			// Census the tier for the diagnostic below.
			if (tier == GTIER_NEAR) ++nNear;
			else if (tier == GTIER_MID) ++nMid;
			else ++nFar;

			// A FAR ghost is refreshed only on its rotor phase, its id.
			if ((tier == GTIER_FAR) && ((id & 1) != g_cullRotor))
				continue;
		}
		else
		{
			// Cull disabled: everything is full-work NEAR.
			++nNear;
		}

		// The wall clamp on an extrapolated ghost's pose, swept once a record.
		if (pose.extrapolated && (pose.exCapMS > 0) && (g_wallTrace != NULL))
		{
			GhostWallTrace & wt = g_wallTrace[id];
			const float accel = (float)pose.exAccelZ;
			if ((wt.ox != pose.ox) || (wt.oy != pose.oy) || (wt.oz != pose.oz)
				|| (wt.vx != pose.vx) || (wt.vy != pose.vy) || (wt.vz != pose.vz)
				|| (wt.accelZ != accel) || (wt.capMS != (WORD)pose.exCapMS))
			{
				wt.ox = pose.ox; wt.oy = pose.oy; wt.oz = pose.oz;
				wt.vx = pose.vx; wt.vy = pose.vy; wt.vz = pose.vz;
				wt.accelZ = accel;
				wt.capMS = (WORD)pose.exCapMS;
				wt.stop = ghost_WallStop(pActor, pose);
			}
			if (wt.stop >= 0.0f)
			{
				const double ex = ((double)pose.x - (double)pose.ox) / 65536.0;
				const double ey = ((double)pose.y - (double)pose.oy) / 65536.0;
				const double along = sqrt(ex * ex + ey * ey);
				if (along > (double)wt.stop)
				{
					const double vx = (double)pose.vx, vy = (double)pose.vy;
					const double speed = sqrt(vx * vx + vy * vy) / 65536.0;	// units/tic
					const double k = (double)wt.stop / along;
					const double t = (double)wt.stop / speed;
					pose.x = pose.ox + (fixed_t)(((double)pose.x - (double)pose.ox) * k);
					pose.y = pose.oy + (fixed_t)(((double)pose.y - (double)pose.oy) * k);
					pose.z = (fixed_t)((double)pose.oz + (double)pose.vz * t
						+ 0.5 * pose.exAccelZ * t * t);
					++g_wallClamped;
				}
			}
		}

		// Smoothing after the wall clamp; under nomonsterinterpolation
		// a monster's walking steps land raw.
		// The fold at this frame's moment is what it draws, with the tic's end folded on from
		// that state; the carried one is const here, so only a packet's fold writes it.
		bool bSlides = false;
		bool bHere = false;
		fixed_t hereNow[3] = { pose.x, pose.y, pose.z };
		if (g_smooth != NULL)
		{
			const SnapSmooth & carried = g_smooth[id];
			SnapSmooth chain = carried;
			SnapGhostPose atFrame;
			if (SampleGhostQuiet(id, renderMS, atFrame))
			{
				ghost_FoldCopy(chain, id, atFrame, renderMS,
					hereNow[0], hereNow[1], hereNow[2]);
				bHere = true;
			}
			ghost_FoldCopy(chain, id, pose, tickEndMS, pose.x, pose.y, pose.z);
			bSlides = ghost_Slides(carried, id, pose,
				nomonsterinterpolation && ((pActor->GetDefault()->flags3 & MF3_ISMONSTER) != 0));
		}

		// The pose of a descending extrapolated ghost stops at its sector's
		// floor plane, 3D floors and step-ups ignored.
		fixed_t useZ = pose.z;
		if (pose.extrapolated && (pose.z < pActor->z))
		{
			const sector_t * const pSec = P_PointInSector(pose.x, pose.y);
			if (pSec != NULL)
			{
				const fixed_t floorZ = pSec->floorplane.ZatPoint(pose.x, pose.y);
				if (useZ < floorZ)
					useZ = floorZ;
			}
		}
		// And the ceiling, for a rising one: the floor clamp mirrored.
		else if (pose.extrapolated && (pose.z > pActor->z))
		{
			const sector_t * const pSec = P_PointInSector(pose.x, pose.y);
			if (pSec != NULL)
			{
				const fixed_t ceilZ = pSec->ceilingplane.ZatPoint(pose.x, pose.y);
				if (useZ + pActor->height > ceilZ)
				{
					useZ = ceilZ - pActor->height;
					++g_ceilClamped;
				}
			}
		}

		// A ghost the smoothing leaves alone slides over each tic, any other
		// drawn where the smoothing put it.
		const bool bNewTic = (g_slideTic != NULL) && (g_slideTic[id] != gametic);
		if (g_slideTic != NULL)
			g_slideTic[id] = gametic;
		const fixed_t want[3] = { pose.x, pose.y, useZ };
		fixed_t pos[3] = { pActor->x, pActor->y, pActor->z };
		fixed_t prev[3] = { pActor->PrevX, pActor->PrevY, pActor->PrevZ };
		ComputeSlideFrame(bSlides && (g_slideTic != NULL), bNewTic, want, pose.posGapTics,
			pos, prev);

		// [Kaori-Chani] The start that makes this frame draw the fold at its own moment;
		// without it the start is the end and the mirror steps as the copy refills.
		if ((bSlides == false) && bHere)
		{
			const double here[3] = { (double)hereNow[0], (double)hereNow[1],
				(double)hereNow[2] };
			ComputeAnchorStart(want, here, ticFrac, prev);
		}
		// Relinked directly and only on a move, SetOrigin's floor scan unread.
		if ((pActor->x != pos[0]) || (pActor->y != pos[1]) || (pActor->z != pos[2]))
		{
			pActor->UnlinkFromWorld();
			pActor->x = pos[0];
			pActor->y = pos[1];
			pActor->z = pos[2];
			pActor->LinkToWorld();
			++nRelink;
		}
		pActor->PrevX = prev[0];
		pActor->PrevY = prev[1];
		pActor->PrevZ = prev[2];
		pActor->PrevAngle = pose.angle;

		pActor->angle = pose.angle;
		pActor->pitch = pose.pitch;

		// Velocity, which only particle trails read.
		pActor->velx = pose.vx;
		pActor->vely = pose.vy;
		pActor->velz = pose.vz;

		// Appearance, from qstate, the mirror keeping its own if unresolved.
		FState * pWireState = ghost_StateFromIndex(pActor, pose.stateIdx);
		const WORD wantSprite = (pWireState != NULL)
			? (WORD)pWireState->sprite : pActor->sprite;
		const BYTE wantFrame = (pWireState != NULL)
			? (BYTE)pWireState->GetFrame() : pActor->frame;

		// The loop census on every tier, before the gate below.
		if (fua_csnap_loopcensus)
		{
			// No per-frame advance: the shadow moves only when a record arrives,
			// by the server-tic delta between that record and the previous one.
			if (pose.stateIdx != g_shadowLastState[id])
			{
				const int gap = (g_shadowLastTic[id] > 0) ? (pose.serverTic - g_shadowLastTic[id]) : 0;
				if (gap >= 0)
					ghost_ShadowScore(id, pWireState, pose.tics, gap);
				g_shadowLastState[id] = pose.stateIdx;
				g_shadowLastTic[id] = pose.serverTic;
			}
		}

		// This mirror's elapsed tics, clamped, for the local state machine.
		int animTics = 0;
		if (g_mirrorAnimTic != NULL)
		{
			const int since = (int)(WORD)((WORD)gametic - g_mirrorAnimTic[id]);
			animTics = (since > 8) ? 8 : since;
			g_mirrorAnimTic[id] = (WORD)gametic;
		}

		if ((int)tier < fua_csnap_localanim)
		{
			// Advanced to now before anything reads it, a ring that missed
			// advances starting over.
			MsRing* const pRing = ms_Ring(id, pActor);
			if (pRing != NULL)
			{
				if ((pRing->lastAnim == 0) || (gametic - pRing->lastAnim > 16) || (bWasAnimating == false))
				{
					pRing->count = 0;
					pRing->bSeed = true;
				}
				pRing->lastAnim = gametic;
			}
			ghost_AdvanceAnim(pActor, animTics, pRing);

			// A resync only on a server animation change or an event.
			const bool bEvent = (g_mirrorEventSeq[id] != pose.eventSeq);
			g_mirrorEventSeq[id] = pose.eventSeq;

			if ((pose.stateIdx != g_mirrorSyncedState[id]) || promotedToNear
				|| bEvent)
			{
				g_mirrorSyncedState[id] = pose.stateIdx;

				// If local animation already arrived at the same frame, it is
				// tracking correctly and there is nothing to correct.
				bool bAgreed = (pActor->sprite == wantSprite) && (pActor->frame == wantFrame);

				// ...unless the two states' chains do not meet.
				if (bAgreed && (pWireState != NULL) && (pActor->state != pWireState)
					&& (ghost_SameChain(pActor->state, pWireState) == false))
					bAgreed = false;

				// Not pulled back from a final state the wire's leads to, which
				// replayed the last transition, an event's included.
				const bool bHoldTerminal = ghost_RestsAhead(pActor, pWireState);
				if (bHoldTerminal && ((bAgreed == false) || bEvent))
				{
					++g_animTerminalHeld;
					if (pRing != NULL)
						++g_msHold;
				}

				if (promotedToNear == false)
				{
					ghost_CensusByState(pActor, pWireState, (BYTE)tier, id);
					if (g_stLastAppearTic != NULL)
					{
						const int gap = pose.serverTic - g_stLastAppearTic[id];
						ghost_CensusGap(pActor, pWireState, gap);
						g_stLastAppearTic[id] = pose.serverTic;
					}
				}

				if (promotedToNear == false)
				{
					const unsigned int t = (tier < 3) ? (unsigned int)tier : 2u;
					if (bAgreed)
					{
						++g_animPredictOk;
						++g_animTierOk[t];
					}
					else
					{
						++g_animPredictMiss;
						++g_animTierMiss[t];
					}
				}
				// An event agreeing on the frame still sets the phase, as a
				// state entered again (a weapon refiring) changes only that.
				if (bAgreed && bEvent && (bHoldTerminal == false))
				{
					FState * const pEvWire = ghost_StateFromIndex(pActor, pose.stateIdx);
					if (pEvWire != NULL)
					{
						// The APPEARANCE clock: qtics is as old as the thing that
						// carried it, not as old as the last record of any kind.
						ms_RecordSet(pRing, pActor, pEvWire, true, false, wantSprite, wantFrame,
							pose.tics, pose.appearAgeTics);
						++g_evPhaseForced;
					}
				}

				if ((bAgreed == false) && (bHoldTerminal == false))
				{
					int steps = -1;
					FState * pMatch = ghost_FindState(pActor, wantSprite, wantFrame, &steps);
					if (promotedToNear == false)
					{
						if (steps >= 0)
						{
							++g_animBehind[(steps < 4) ? steps : 4];
						}
						else
						{
							// Walking forward from the wire's frame finds a
							// mirror that ran past it.
							int back = -1;
							FState * pFromWire = ghost_FindState(pActor, wantSprite, wantFrame);
							if (pFromWire != NULL)
							{
								const FState * pWalk = pFromWire->GetNextState();
								for (int step = 0; (step < 8) && (pWalk != NULL); ++step)
								{
									if (pWalk == pActor->state) { back = step + 1; break; }
									pWalk = pWalk->GetNextState();
								}
							}
							if (back >= 0)
								++g_animAhead;
							else
								++g_animOffChain;
						}
					}

					// The state the wire names over the search's, which at a
					// branch favours the chain the client is on.
					FState * pWire = ghost_StateFromIndex(pActor, pose.stateIdx);
					++g_stateRecs;

					// ghost_SetStateSafe takes a NULL match and a dead end.
					ms_RecordSet(pRing, pActor, (pWire != NULL) ? pWire : pMatch, pWire != NULL, false,
						wantSprite, wantFrame, pose.tics, pose.appearAgeTics);
				}
			}

			// A stale wire and a disagreeing mirror take the wire's frame, once
			// per wire frame (g_mirrorHardFrame).
			if ((pose.ageMS > (unsigned int)GHOST_ANIM_RESYNC_MS)
				&& (pose.stateIdx != g_mirrorHardFrame[id])
				&& ((pActor->sprite != wantSprite) || (pActor->frame != wantFrame)))
			{
				g_mirrorHardFrame[id] = pose.stateIdx;
				FState* pHardWire = ghost_StateFromIndex(pActor, pose.stateIdx);
				// The same guard, with no frame forced after the set, which the
				// aging has moved on.
				if (ghost_RestsAhead(pActor, pHardWire))
				{
					++g_animTerminalHeldHard;
					if (pRing != NULL)
						++g_msHold;
				}
				else
					ms_RecordSet(pRing, pActor, (pHardWire != NULL) ? pHardWire
						: ghost_FindState(pActor, wantSprite, wantFrame), pHardWire != NULL, true,
						wantSprite, wantFrame, pose.tics, pose.appearAgeTics);
				g_mirrorSyncedState[id] = pose.stateIdx;
			}
		}
		else
		{
			// At or above the level the wire frame, the synced marker kept in
			// step for a later promotion.
			pActor->sprite = wantSprite;
			pActor->frame = wantFrame;
			g_mirrorSyncedState[id] = pose.stateIdx;
		}

		// What the mirror draws after the advance and resync, on change.
		if ((g_recLastDrawn != NULL) && (fua_csnap_recmirror > 0)
			&& ((id % (unsigned int)(int)fua_csnap_recmirror) == 0))
		{
			// The drawn sprite and frame, as qstate is kept only where
			// local animation runs.
			const DWORD shown = ((DWORD)pActor->sprite << 8) | (DWORD)pActor->frame;
			if (shown != g_recLastDrawn[id])
			{
				g_recLastDrawn[id] = shown;
				RecorderMirror(id,
					QStateForState(pActor->GetClass(), pActor->state),
					pActor->sprite, pActor->frame,
					(pActor->tics >= 0 && pActor->tics < 255) ? (BYTE)pActor->tics : (BYTE)255);
			}
		}

		pActor->Translation = TranslationFromState(pose.renderstate);
		pActor->alpha = AlphaFromState(pose.renderstate);

		BYTE styleIndex = StyleIndexFromState(pose.renderstate);
		// STYLE_None means do not draw, and a mirror holds a real renderstate
		// before it draws.
		if (styleIndex >= STYLE_Count)
			styleIndex = (BYTE)STYLE_Normal;
		pActor->RenderStyle = LegacyRenderStyles[styleIndex];

		// The client's own copy hidden, catching any the packet path missed.
		ghost_HideLegacy(id, pActor, true);

		// Particle effects, which only P_RunEffects reads.
		pActor->effects = UnpackEffects(pose.effects);

		// Misc bits (see snap_CaptureActor).
		if (pose.misc & SNAPMISC_INVISIBLE)
			pActor->renderflags |= RF_INVISIBLE;
		else
			pActor->renderflags &= ~RF_INVISIBLE;

		// A player mirror's crouch, assigned rather than integrated, its
		// height following so the solid box is short.
		{
			const int crouchPlayer = MirrorPlayerNum(pActor);
			if ((crouchPlayer >= 0) && (crouchPlayer < MAXPLAYERS))
			{
				const fixed_t cf = (fixed_t)CrouchFromWire(
					(WORD)((pose.misc & SNAPMISC_CROUCH_MASK) >> SNAPMISC_CROUCH_SHIFT),
					(int)FRACUNIT);	// [Kaori-Chani] the raw unit, as the codec wants
				players[crouchPlayer].crouchfactor = cf;
				pActor->height = FixedMul(pActor->GetDefault()->height, cf);
			}
		}

		// Trail smoke follows this client's cl_rockettrails bit 2, matched
		// through the replacement and by descent.
		if (((cl_rockettrails & 2) == 0) && ghost_IsTrailSmoke(pActor->GetClass()))
			pActor->renderflags |= RF_INVISIBLE;

	}

	// Every remote player's own actor follows its ghost once a gametic, as
	// players[].mo is what everything else reads.
	if (g_realPoseTic != gametic)
	{
		g_realPoseTic = gametic;
		for (unsigned int pl = 0; pl < (unsigned int)MAXPLAYERS; ++pl)
		{
			if ((int)pl == consoleplayer)
				continue;
			if ((playeringame[pl] == false) || (players[pl].mo == NULL))
				continue;

			const unsigned int rid = (unsigned int)IDList<AActor>::reservedIDForPlayer((int)pl);
			if ((rid == 0) || (rid >= SNAP_MAXID))
				continue;

			SnapGhostPose rpose;
			if (SampleGhost(rid, renderMS, rpose) == false)
				continue;

			AActor * const pReal = players[pl].mo;
			if ((pReal->x == rpose.x) && (pReal->y == rpose.y) && (pReal->z == rpose.z))
				continue;

			CLIENT_MoveThing(pReal, rpose.x, rpose.y, rpose.z);
			pReal->angle = rpose.angle;
			++g_realPosed;
		}
	}

	// Destroys mirrors whose ghost has left, by swap-with-last removal.
	for (unsigned int k = 0; k < g_mirrorIds.Size(); )
	{
		const unsigned int id = g_mirrorIds[k];
		const bool leaving = (GhostPresent(id) == false) && (g_mirror[id] != NULL);

		if (leaving)
		{
			{
				bool bResolved = false, bChanged = false;
				if ((GhostTarget(id, bResolved, bChanged) != 0) && (bResolved == false))
					++g_tgtNever;
			}
			ghost_ShowLegacy(id, false);
			// An id is handed out again, so a set left here would dress whatever
			// takes it next.
			g_ghostProps.Remove(id);
			// Unpin before destroying (see ghost_CreateMirror): a mirror whose
			// ghost has genuinely left should be collectable again.
			g_mirror[id]->ObjectFlags &= ~OF_Fixed;
			g_mirror[id]->Destroy();
			g_mirror[id] = NULL;
			ms_Forget(id);
			--g_mirrorCount;
		}
		else if (g_mirror[id] != NULL)
		{
			// Still present and alive: keep it, advance.
			++k;
			continue;
		}
		// else: reference already dropped elsewhere (level teardown) - fall
		// through and remove the dead entry.

		// Slot k takes the tail and is examined again.
		const unsigned int last = g_mirrorIds.Size() - 1;
		if (k != last)
			g_mirrorIds[k] = g_mirrorIds[last];
		unsigned int popped;
		g_mirrorIds.Pop(popped);
	}

	// Which ids hold a mirror this frame, telling next frame's recreate from
	// a new ghost.
	if (g_hadMirror != NULL)
	{
		memset(g_hadMirror, 0, SNAP_MAXID);
		for (unsigned int m = 0; m < g_mirrorIds.Size(); ++m)
			g_hadMirror[g_mirrorIds[m]] = 1;
	}

	// This frame's per-tier counts and elapsed time, averaged in the report.
	g_ghostSelfRaw += (QWORD)(TIC_GetRawCounter() - ghostStartRaw);

	g_ghostNearAccum += nNear;
	g_ghostMidAccum += nMid;
	g_ghostFarAccum += nFar;
	g_ghostRelinkAccum += nRelink;
	g_ghostSolidAccum += nSolid;
	g_ghostSolidCorpseAccum += nSolidCorpse;
	g_ghostSolidChangeAccum += nSolidChange;
	if (nSolid > g_ghostSolidMax)
		g_ghostSolidMax = nSolid;
	g_ghostCreateAccum += nCreate;
	g_ghostRecreateAccum += nRecreate;
	++g_ghostCensusFrames;
	if (nNear > g_ghostNearMax)
		g_ghostNearMax = nNear;
	if (nRelink > g_ghostRelinkMax)
		g_ghostRelinkMax = nRelink;

	// After this frame's mirrors exist, so a sound finds the one just created.
	ghost_AttachPointSounds();

	// [Kaori-Chani] Both at I_MSTime's width, so the subtraction wraps with it; widening to
	// 64 bits first breaks that.
	const unsigned int nowMS2 = I_MSTime();
	if (g_ghostReportMS == 0)
		g_ghostReportMS = nowMS2;
	if ((nowMS2 - g_ghostReportMS) >= 5000)
	{
		ghost_Report();
		g_ghostReportMS = nowMS2;
	}
}

//*****************************************************************************
// CLIENT_ReadActorFromNetID's mirror fallback, kept here with its counters.

// The sound counters, defined in cl_main.cpp beside the sound trace that reads them.
extern unsigned int g_ghostSndLegacy, g_ghostSndMirror, g_ghostSndLost;
extern unsigned int g_ghostSndWrongClass, g_ghostSndLostLeft;
extern unsigned int g_ghostSndLostNoMirror, g_ghostSndLostNoGhost;
extern char g_ghostSndLegacyClass[64];
// Blood and conversation resolve the same way but keep their own counters, so each family's
// figures stay comparable with what was measured for it.
unsigned int g_ghostBloodLegacy = 0, g_ghostBloodMirror = 0, g_ghostBloodLost = 0;
unsigned int g_ghostConvLegacy = 0, g_ghostConvMirror = 0, g_ghostConvLost = 0;

// How recently an entity may have left and still have its sound counted against that
// departure rather than against never having been known.
#define CLIENT_DEPARTED_SOUND_MS	500

// The three families share this path and keep their own counters, and a miss records which
// of the three causes it was.
static void ghost_ResolveCounted( unsigned short netID, const PClass *subclass,
	AActor *&actor, const char *commandName )
{
	const bool bBlood = ( strnicmp( commandName, "SpawnBlood", 10 ) == 0 );
	const bool bConv = ( stricmp( commandName, "StartConversation" ) == 0 );

	if ( actor != NULL )
	{
		if ( bBlood )
			++g_ghostBloodLegacy;
		else if ( bConv )
			++g_ghostConvLegacy;
		else
		{
			++g_ghostSndLegacy;
			if (( g_ghostSndLegacyClass[0] == '\0' ) && ( actor->GetClass( ) != NULL ))
			{
				// Copied rather than pointed at: a name table entry outlives this
				// call, but a copy cannot be wrong about that.
				strncpy( g_ghostSndLegacyClass, actor->GetClass( )->TypeName.GetChars( ),
					sizeof( g_ghostSndLegacyClass ) - 1 );
				g_ghostSndLegacyClass[sizeof( g_ghostSndLegacyClass ) - 1] = '\0';
			}
		}
		return;
	}

	actor = MirrorForNetID( netID );
	if ( actor == NULL )
	{
		if ( bBlood )
			++g_ghostBloodLost;
		else if ( bConv )
			++g_ghostConvLost;
		else
		{
			++g_ghostSndLost;
			// Three causes with opposite fixes, in this order because a departed
			// ghost is also absent and would otherwise read as never known.
			if ( LeftWithin( netID, CLIENT_DEPARTED_SOUND_MS ))
				++g_ghostSndLostLeft;
			else if ( GhostPresent( netID ))
				++g_ghostSndLostNoMirror;
			else
				++g_ghostSndLostNoGhost;
		}
		return;
	}

	if ( actor->IsKindOf( subclass ) == false )
	{
		++g_ghostSndWrongClass;		// rejected by the class test below
		return;
	}

	if ( bBlood )
		++g_ghostBloodMirror;
	else if ( bConv )
		++g_ghostConvMirror;
	else
		++g_ghostSndMirror;
}

// The commands whose actor may be a mirror, every other
// one naming a snapshot-owned actor being dropped deliberately; see the module's README.
static bool ghost_CommandAcceptsGhost( const char *commandName )
{
	if ( commandName == NULL )
		return false;

	return ( stricmp( commandName, "SoundActor" ) == 0 )
		|| ( stricmp( commandName, "SoundActorIfNotPlaying" ) == 0 )
		|| ( stricmp( commandName, "StopSound" ) == 0 )
		|| ( stricmp( commandName, "SpawnBlood" ) == 0 )
		|| ( stricmp( commandName, "SpawnBloodSplatter" ) == 0 )
		|| ( stricmp( commandName, "SpawnBloodSplatter2" ) == 0 )
		|| ( stricmp( commandName, "StartConversation" ) == 0 );
}

void zx::netsnapshot::ResolveThroughMirror( unsigned short netID, const PClass *subclass,
	AActor *&actor, const char *commandName )
{
	if ( ghost_CommandAcceptsGhost( commandName ))
		ghost_ResolveCounted( netID, subclass, actor, commandName );
}

// The [snap] report, which clears the counts behind each line it prints.
static void ghost_Report()
{
	// Read and reset once: the frame-order and movers lines both print it,
	// so neither may depend on which prints first.
	const unsigned int mvStale = MoverStaleRecords(true);
	const unsigned int mvBeforeReset = MoverBeforeResetRecords(true);
	const unsigned int tailRequested = CLIENT_TailPacketsRequested(true);

	if (g_ghostCensusFrames > 0)
	{
		const double inv = 1.0 / (double)g_ghostCensusFrames;
		Printf("[snap] ghostframe: %u mirrors | near %.0f (max %u) mid %.0f far %.0f | "
			"relink %.0f (max %u) create %.1f (recreate %.1f) | gc %.0f-%.0fMB | "
			"ghostMS %.1f (%.2f/f) | sim %.2f/f render %.2f/f | tics %.2f/f | "
			"think %.2f/f fx %.2f/f parse %.2f/f gtick %.2f/f netupd %.2f/f | %u frames/5s\n",
			g_mirrorCount,
			(double)g_ghostNearAccum * inv, g_ghostNearMax,
			(double)g_ghostMidAccum * inv, (double)g_ghostFarAccum * inv,
			(double)g_ghostRelinkAccum * inv, g_ghostRelinkMax,
			(double)g_ghostCreateAccum * inv,
			(double)g_ghostRecreateAccum * inv,
			(double)g_gcAllocMin / (1024.0 * 1024.0),
			(double)g_gcAllocMax / (1024.0 * 1024.0),
			ghost_RawToMS(g_ghostSelfRaw), ghost_RawToMS(g_ghostSelfRaw) * inv,
			ghost_RawToMS(g_frameSimRaw) * inv,
			ghost_RawToMS(g_frameRenderRaw) * inv,
			(double)g_frameTicsRun * inv,
			ghost_RawToMS(g_ticThinkersRaw) * inv,
			ghost_RawToMS(g_ticEffectsRaw) * inv,
			ghost_RawToMS(g_applyDeltaRaw) * inv,
			ghost_RawToMS(g_gtickerRaw) * inv,
			ghost_RawToMS(g_netupdateRaw) * inv,
			g_ghostCensusFrames);
		Printf("[snap] player mirrors: %u player ghosts, %u excluded\n",
			g_playerGhostSeen, g_playerGhostExcluded);
		{
			unsigned int gU = 0, gE = 0, gZ = 0, gR = 0, gN = 0;
			double gMean = 0.0, gWorst = 0.0;
			GravStats(gU, gE, gZ, gMean, gWorst, gR, gN, true);
			unsigned int lE = 0, lN = 0, lD = 0;
			ClientLawStats(lE, lN, lD, true);
			if ((gU | gE) != 0)
				Printf("[snap] gravity ballistic: %u records, %u extrapolated"
					" (%u with no accel) | accel mean %.2f worst %.2f u/t/t"
					" | %u refused, %u unconfirmed | by the law %u, no law %u,"
					" measured disagreeing %u\n",
					gU, gE, gZ, gMean, gWorst, gR, gN, lE, lN, lD);
		}
		if ((g_sm.folds[0] | g_sm.folds[1] | g_sm.snaps[0] | g_sm.snaps[1]
			| g_sm.zero | g_sm.movingFrames) != 0)
		{
			Printf("[snap] smoothing: stepped %u folds (mean %.1f u), %u snaps,"
				" residual <0.5 %u <2 %u <8 %u 8+ %u | extrapolated %u folds"
				" (mean %.2f u), %u snaps, residual <0.5 %u <2 %u <8 %u 8+ %u"
				" | %u tics off target, %u stepped drawn OUTSIDE its records | %u records"
				" moved nothing | extrapolated drawn REVERSED %u of %u moving tics\n",
				g_sm.folds[0], (g_sm.folds[0] > 0) ? g_sm.foldSum[0] / g_sm.folds[0] : 0.0,
				g_sm.snaps[0], g_sm.resid[0][0], g_sm.resid[0][1], g_sm.resid[0][2],
				g_sm.resid[0][3], g_sm.folds[1],
				(g_sm.folds[1] > 0) ? g_sm.foldSum[1] / g_sm.folds[1] : 0.0,
				g_sm.snaps[1], g_sm.resid[1][0], g_sm.resid[1][1], g_sm.resid[1][2],
				g_sm.resid[1][3], g_sm.offFrames, g_sm.past, g_sm.zero, g_sm.reversed,
				g_sm.movingFrames);
			// Ballistic, turning, then bodies, each dated by the model then by
			// arrival, an empty cell left out.
			static const unsigned int order[SNAP_SMOOTH_KINDS] =
				{ SNAP_SMOOTH_BALLISTIC, SNAP_SMOOTH_TURNING, SNAP_SMOOTH_BODY };
			char szCell[320];
			for (unsigned int i = 0; i < SNAP_SMOOTH_KINDS * 2; ++i)
			{
				if (ComputeSmoothCellText(g_sm, order[i / 2], 1 - (i & 1), "frames", szCell,
					sizeof(szCell)))
					Printf("[snap] smoothing by kind: %s\n", szCell);
			}
		}
		memset(&g_sm, 0, sizeof(g_sm));
		if ((g_wallClamped | g_wallTraces | g_ceilClamped) != 0)
		{
			Printf("[snap] extrap clamps: %u wall, %u ceiling (frames) | %u wall traces\n",
				g_wallClamped, g_ceilClamped, g_wallTraces);
			g_wallClamped = g_wallTraces = g_ceilClamped = 0;
		}
		if (g_evPhaseForced != 0)
		{
			Printf("[snap] event phase: %u refreshes the frame test would"
				" have skipped\n", g_evPhaseForced);
			g_evPhaseForced = 0;
		}
		{
			// What the decode routed against what the sink applied.
			unsigned int psApplied = 0, psNoSink = 0;
			ClientPSpriteStats(psApplied, psNoSink, true);
			if ((psApplied | psNoSink | g_psprApply | g_psprBadClass
				| g_psprBadState) != 0)
				Printf("[snap] psprite apply: %u routed, %u applied"
					" | %u no sink, %u bad class, %u bad state\n",
					psApplied, g_psprApply,
					psNoSink, g_psprBadClass, g_psprBadState);
			g_psprApply = g_psprBadClass = g_psprBadState = 0;
		}
		{
			const unsigned int tot = g_animPredictOk + g_animPredictMiss;
			if (tot > 0)
			{
				if (fua_csnap_loopcensus)
				{
					// One line per gap bucket, then the aggregate.
					unsigned int tot[GHOST_LC_OUTCOMES] = { 0, 0, 0, 0 };
					for (unsigned int b = 0; b < GHOST_LC_BUCKETS; ++b)
					{
						unsigned int n = 0;
						for (unsigned int o = 0; o < GHOST_LC_OUTCOMES; ++o)
						{
							n += g_lcBucket[b][o];
							tot[o] += g_lcBucket[b][o];
						}
						if (n == 0)
							continue;
						Printf("[snap] loop census gap %-6s: %6u samples, %3.0f%% derivable"
							" | behind %u ahead %u OFF-CHAIN %u (%.0f%%)\n",
							g_szLcBucket[b], n,
							100.0 * (double)g_lcBucket[b][0] / (double)n,
							g_lcBucket[b][1], g_lcBucket[b][2], g_lcBucket[b][3],
							100.0 * (double)g_lcBucket[b][3] / (double)n);
					}
					const unsigned int lcTot = tot[0] + tot[1] + tot[2] + tot[3];
					if ((lcTot + g_lcNoIndex) > 0)
						Printf("[snap] loop census ALL: %u samples, %.0f%% derivable,"
							" %u OFF-CHAIN, %u no index\n", lcTot,
							(lcTot > 0) ? (100.0 * (double)tot[0] / (double)lcTot) : 0.0,
							tot[3], g_lcNoIndex);
					memset(g_lcBucket, 0, sizeof(g_lcBucket));
					g_lcNoIndex = 0;
				}

				Printf("[snap] localanim: %u predicted, %u missed (%.0f%% correct, level %d)"
					" | behind 1:%u 2:%u 3:%u 4+:%u ahead:%u offchain:%u"
					" | final state held %u, %u stale\n",
					g_animPredictOk, g_animPredictMiss, 100.0 * g_animPredictOk / (double)tot,
					(int)fua_csnap_localanim,
					g_animBehind[1], g_animBehind[2], g_animBehind[3], g_animBehind[4],
					g_animAhead, g_animOffChain, g_animTerminalHeld, g_animTerminalHeldHard);

				ghost_PrintTierSplit("localanim", g_animTierOk, g_animTierMiss);
				if (g_stateRecs > 0)
				{
					{
						// Set here too, so an unwritten out-param reads 0.
						unsigned int evA = 0, evO = 0, evU = 0, evS = 0, evN = 0;
						unsigned int evSP = 0, evD = 0;
						ClientEventStats(evA, evO, evU, evS, evN, evSP, evD);
						if ((evA > 0) || (evO > 0) || (evU > 0) || (evS > 0) || (evN > 0) || (evD > 0))
							Printf("[snap] events: %u applied | %u not animating (mirror cannot"
								" animate out) | %u STALE | %u stale POSITION only"
								" | %u ORPHAN | %u payloads ABANDONED on an unknown"
								" event type | %u duplicate copies\n",
								evA, evN, evS, evSP, evO, evU, evD);

						unsigned int evC = 0, evCP = 0, evCS = 0;
						EventCarryStats(evC, evCP, evCS);
						if ((evA > 0) || (evC > 0))
						{
							Printf("[snap] event carry: %u pushes kept an event's"
								" values the model lacks (pos %u, state %u)\n",
								evC, evCP, evCS);
						}

						unsigned int evAge[5], evDur[4], evExp = 0;
						EventAgeStats(evAge, evDur, evExp);
						if (evA > 0)
							Printf("[snap] event age (tics): <2 %u | <4 %u | <7 %u | <12 %u"
								" | 12+ %u || state left: <3 %u <7 %u <15 %u 15+ %u"
								" || EXPIRED on arrival %u (%.0f%%)\n",
								evAge[0], evAge[1], evAge[2], evAge[3], evAge[4],
								evDur[0], evDur[1], evDur[2], evDur[3],
								evExp, 100.0 * (double)evExp / (double)evA);

						unsigned int mNo, mEx, mCap, mAge, mMinB, mMinN, mSB;
						MissileStats(mNo, mEx, mCap, mAge, mMinB, mMinN, mSB);
						char szMinB[16], szMinN[16];
						if (mMinB == 0xFFFFFFFF)	strcpy(szMinB, "none");
						else	mysnprintf(szMinB, 16, "%u", mMinB);
						if (mMinN == 0xFFFFFFFF)	strcpy(szMinN, "none");
						else	mysnprintf(szMinN, 16, "%u", mMinN);
						if ((mNo > 0) || (mEx > 0) || (mCap > 0))
							Printf("[snap] missiles: %u stepped (no bit), %u extrap, "
								"%u FROZEN at cap %dms | age worst %u, min-bit %s, min-nobit %s"
								" | sampler: %u ballistic\n",
								mNo, mEx, mCap, (int)MissileCapMS(), mAge, szMinB, szMinN,
								mSB);

						// Fetched always, so no window carries into the next.
						SnapMissileCensus mc;
						MissileCensus(mc, true);
						if ((mc.ageBucket[0] | mc.ageBucket[1] | mc.ageBucket[2]
							| mc.ageBucket[3] | mc.ageBucket[4]) != 0)
						{
							// cappedFar is a scheduling result - outside the server's
							// missile band.
							char szAhead[128];
							szAhead[0] = '\0';
							if ((mc.aheadInterp | mc.aheadFirst | mc.aheadJump) != 0)
								mysnprintf(szAhead, sizeof(szAhead), " || dated ahead: %u between"
									" records, held %u with none earlier, %u at a jump",
									mc.aheadInterp, mc.aheadFirst, mc.aheadJump);
							Printf("[snap] missile age (ballistic): <100 %u | <250 %u | "
								"<500 %u | <1000 %u | 1000+ %u || capped near %u far %u%s\n",
								mc.ageBucket[0], mc.ageBucket[1], mc.ageBucket[2],
								mc.ageBucket[3], mc.ageBucket[4],
								mc.cappedNear, mc.cappedFar, szAhead);
						}

						// Sized off the previous record, so it does not move
						// with the cap.
						{
							SnapBlindPredict bp;
							BlindPredict(bp, true);
							unsigned int bpTot = 0;
							for (int q = 0; q < CLENT_BLIND_BUCKETS; ++q)
								bpTot += bp.samples[q] + bp.turned[q];
							if (bpTot > 0)
							{
								double bpAvg[CLENT_BLIND_BUCKETS];
								double bpBad[CLENT_BLIND_BUCKETS];
								for (int q = 0; q < CLENT_BLIND_BUCKETS; ++q)
								{
									bpAvg[q] = (bp.samples[q] > 0)
										? (bp.errSum[q] / (double)bp.samples[q]) : 0.0;
									bpBad[q] = (bp.samples[q] > 0)
										? (100.0 * (double)bp.over64[q] / (double)bp.samples[q]) : 0.0;
								}
								Printf("[snap] blind predict: <4t %u@%.0fu | 4-8t %u@%.0fu"
									" | 9-17t %u@%.0fu | 18-35t %u@%.0fu | 36t+ %u@%.0fu"
									"  (samples @ mean error, flying unaided from the"
									" previous record)\n",
									bp.samples[0], bpAvg[0], bp.samples[1], bpAvg[1],
									bp.samples[2], bpAvg[2], bp.samples[3], bpAvg[3],
									bp.samples[4], bpAvg[4]);
								Printf("[snap] blind predict past 64u: <4t %.0f%% | 4-8t %.0f%%"
									" | 9-17t %.0f%% | 18-35t %.0f%% | 36t+ %.0f%%"
									" || bit cleared mid-gap: %u %u %u %u %u\n",
									bpBad[0], bpBad[1], bpBad[2], bpBad[3], bpBad[4],
									bp.turned[0], bp.turned[1], bp.turned[2],
									bp.turned[3], bp.turned[4]);
							}
						}

						MissileStatsReset();
						ClientEventStatsReset();
					}

					const unsigned int stTot = g_stAgree + g_stBehind[0] + g_stBehind[1]
						+ g_stBehind[2] + g_stBehind[3] + g_stAhead + g_stOffChain;
					if (stTot > 0)
						Printf("[snap] bystate: %u samples, %.0f%% correct | behind 1:%u 2:%u 3:%u"
							" 4:%u | ahead<=4:%u DIVERGED:%u | %u no index\n",
							stTot, 100.0 * (double)g_stAgree / (double)stTot,
							g_stBehind[0], g_stBehind[1], g_stBehind[2], g_stBehind[3],
							g_stAhead, g_stOffChain, g_stNoIdx);
					if (stTot > 0)
						ghost_PrintTierAgree("bystate", g_stTierAgree, g_stTierSamp);

					// The control group against the selected population.
					{
						static const char * const szEx[2] = { "suppressible", "EXEMPT" };
						for (int ex = 0; ex < 2; ++ex)
						{
							const unsigned int tot = g_stSplitSamp[ex][0]
								+ g_stSplitSamp[ex][1] + g_stSplitSamp[ex][2];
							if (tot == 0)
								continue;
							Printf("[snap] bystate %s: near %u%% (%u) | mid %u%% (%u)"
								" | far %u%% (%u)\n", szEx[ex],
								g_stSplitSamp[ex][0] ? g_stSplitAgree[ex][0] * 100
									/ g_stSplitSamp[ex][0] : 0, g_stSplitSamp[ex][0],
								g_stSplitSamp[ex][1] ? g_stSplitAgree[ex][1] * 100
									/ g_stSplitSamp[ex][1] : 0, g_stSplitSamp[ex][1],
								g_stSplitSamp[ex][2] ? g_stSplitAgree[ex][2] * 100
									/ g_stSplitSamp[ex][2] : 0, g_stSplitSamp[ex][2]);
						}
					}

					// Correctness against the wire by time run unaided.
					Printf("[snap] bystate by gap:");
					for (int gb = 0; gb < GST_GAPS; ++gb)
					{
						if (g_stGapSamp[gb] == 0)
							continue;
						if (gb + 1 < GST_GAPS)
							Printf(" <=%dt %u%% (%u)", g_stGapMax[gb],
								g_stGapAgree[gb] * 100 / g_stGapSamp[gb],
								g_stGapSamp[gb]);
						else
							Printf(" >%dt %u%% (%u)", g_stGapMax[GST_GAPS - 2],
								g_stGapAgree[gb] * 100 / g_stGapSamp[gb],
								g_stGapSamp[gb]);
					}
					Printf("\n");
				}
			}
			g_animPredictOk = g_animPredictMiss = 0;
			memset(g_animTierOk, 0, sizeof(g_animTierOk));
			memset(g_animTierMiss, 0, sizeof(g_animTierMiss));
			memset(g_animBehind, 0, sizeof(g_animBehind));
			g_animOffChain = g_animAhead = 0;
			g_animTerminalHeld = g_animTerminalHeldHard = 0;
			g_stateRecs = 0;
			g_stAgree = g_stAhead = g_stOffChain = g_stNoIdx = 0;
			memset(g_stTierAgree, 0, sizeof(g_stTierAgree));
			memset(g_stSplitAgree, 0, sizeof(g_stSplitAgree));
			memset(g_stSplitSamp, 0, sizeof(g_stSplitSamp));
			memset(g_stGapSamp, 0, sizeof(g_stGapSamp));
			memset(g_stGapAgree, 0, sizeof(g_stGapAgree));
			memset(g_stTierSamp, 0, sizeof(g_stTierSamp));
			memset(g_stBehind, 0, sizeof(g_stBehind));
		}

		// nb = how many of the hidden actors we actually unlinked from the
		// blockmap, so a dead gate is visible rather than inferred.
		Printf("[snap] mirror lookup: %u ok, %u empty, %u badid, %u notable | mirrors %u\n",
			g_mirrorLookupOk, g_mirrorLookupEmpty, g_mirrorLookupBadId,
			g_mirrorLookupNoTable, g_mirrorCount);

		// Sound resolution and the double-play guard, as fua_csnap_soundinfo
		// (`suppressed`: local sounds dropped on a ghosted legacy actor).
		{
			extern unsigned int g_ghostSndLegacy, g_ghostSndMirror;
			extern unsigned int g_ghostSndWrongClass, g_ghostSndLost;
			extern unsigned int g_ghostSndLostLeft, g_ghostSndLostNoMirror;
			extern unsigned int g_ghostSndLostNoGhost, g_ghostSndPositional;
			extern char g_ghostSndLegacyClass[64];
			extern unsigned int g_sndLocalGhost, g_sndLocalOther, g_sndLocalUngated;
			extern unsigned int g_ghostBloodLegacy, g_ghostBloodMirror, g_ghostBloodLost;
			extern unsigned int g_ghostConvLegacy, g_ghostConvMirror, g_ghostConvLost;
			// The two unsuppressed counts among them, so the line prints where the guard looked
			// at sounds and suppressed none, which is what it reads below level 3.
			if ((g_ghostSndLegacy | g_ghostSndMirror | g_ghostSndWrongClass | g_ghostSndLost
				| g_sndLocalGhost | g_sndLocalOther | g_sndLocalUngated) != 0)
			{
				Printf("[snap] sound: %u to mirror, %u legacy%s%s, %u wrong-class,"
					" %u LOST | local %u suppressed (%s), %u left to stock,"
					" %u other\n",
					g_ghostSndMirror, g_ghostSndLegacy,
					(g_ghostSndLegacyClass[0] != '\0') ? " first " : "",
					g_ghostSndLegacyClass,
					g_ghostSndWrongClass,
					g_ghostSndLost, g_sndLocalGhost,
					fua_csnap_nolocalsound ? "guard ON" : "GUARD OFF",
					g_sndLocalUngated, g_sndLocalOther);
				if (g_ghostSndLost != 0)
					Printf("[snap] sound LOST detail: %u left recently "
						"(%u PLAYED positionally, %u still dropped), "
						"%u ghost but no mirror, %u never known\n",
						g_ghostSndLostLeft, g_ghostSndPositional,
						(g_ghostSndLostLeft >= g_ghostSndPositional)
							? (g_ghostSndLostLeft - g_ghostSndPositional) : 0,
						g_ghostSndLostNoMirror, g_ghostSndLostNoGhost);
			}
			g_ghostSndLegacy = g_ghostSndMirror = 0;
			g_ghostSndWrongClass = g_ghostSndLost = 0;
			g_ghostSndLostLeft = g_ghostSndLostNoMirror = g_ghostSndLostNoGhost = 0;
			g_ghostSndPositional = 0;
			g_ghostSndLegacyClass[0] = '\0';
			// Each family on its own line and reset with the sound's, so one window's figures
			// stay comparable across them.
			if (( g_ghostBloodLegacy | g_ghostBloodMirror | g_ghostBloodLost ) != 0 )
				Printf("[snap] blood: %u to mirror, %u legacy, %u LOST\n",
					g_ghostBloodMirror, g_ghostBloodLegacy, g_ghostBloodLost);
			if (( g_ghostConvLegacy | g_ghostConvMirror | g_ghostConvLost ) != 0 )
				Printf("[snap] conversation: %u to mirror, %u legacy, %u LOST\n",
					g_ghostConvMirror, g_ghostConvLegacy, g_ghostConvLost);
			g_ghostBloodLegacy = g_ghostBloodMirror = g_ghostBloodLost = 0;
			g_ghostConvLegacy = g_ghostConvMirror = g_ghostConvLost = 0;
			g_sndLocalGhost = g_sndLocalOther = g_sndLocalUngated = 0;
		}

		// Item respawns answered behind a mirror rather than on the hidden copy.
		if (g_respawnOnMirror != 0)
		{
			Printf("[snap] item respawns: %u answered at the ghost, the hidden copy left"
				" as it was\n", g_respawnOnMirror);
			g_respawnOnMirror = 0;
		}

		// Properties off a class default: what arrived, and where it went.
		{
			unsigned int applied = 0, bad = 0, toMirror = 0, held = 0;
			PropsEventStats(applied, bad, true);
			PropsStats(toMirror, held, true);
			if ((applied | bad | toMirror | held) != 0)
				Printf("[snap] properties: %u sets arrived (%u REFUSED) | %u onto a mirror,"
					" %u held for one not made yet | %u kept\n", applied, bad, toMirror, held,
					PropsKept());
		}

		// Point sounds: on the mirror, moved onto it, or left at the point.
		if ((g_ptSndOnMirror | g_ptSndAttached | g_ptSndAttachedLate | g_ptSndRefused
			| g_ptSndEnded | g_ptSndCap | g_ptSndBusy) != 0)
		{
			Printf("[snap] point sounds: %u on the mirror at once, %u re-attached (+%u late),"
				" %u refused at the start, %u ended at the point, %u not tracked (list full),"
				" %u mirror channel busy | %u pending\n",
				g_ptSndOnMirror, g_ptSndAttached, g_ptSndAttachedLate, g_ptSndRefused,
				g_ptSndEnded, g_ptSndCap, g_ptSndBusy, g_pointSounds.Size());
			g_ptSndOnMirror = g_ptSndAttached = g_ptSndAttachedLate = g_ptSndRefused = 0;
			g_ptSndEnded = g_ptSndCap = g_ptSndBusy = 0;
		}

		// Whether this map's sounds come by index, and any index dropped
		// for arriving without a matched table (a protocol fault).
		{
			unsigned int idPlayed = 0, idDropped = 0, serverN = 0, localN = 0;
			bool bKnown = false, bMatch = false;
			SoundIdStats(idPlayed, idDropped, serverN, localN, bKnown, bMatch, true);
			const unsigned int idOtherMap = SoundOtherMap(true);
			if (bKnown || (idPlayed | idDropped | idOtherMap) != 0)
				Printf("[snap] sound ids: table %s (server %u names, here %u)"
					" | %u played by id, %u DROPPED, %u for another map\n",
					bKnown ? (bMatch ? "matched" : "DIFFERS, names") : "not received",
					serverN, localN, idPlayed, idDropped, idOtherMap);
		}

		// Cosmetic events, and the catch-up, which also runs spawns' children.
		if ((g_cosEvents > 0) || (g_cosCatchUpTicks > 0))
		{
			Printf("[snap] cosmetic events: %u received (%u blood), %u copies (%u plain) | dropped:"
				" %u unknown class, %u originator unknown, %u not this map | %u states not found | catch-up %u tics,"
				" %u sounds held back | copied most:%s\n", g_cosEvents, g_cosBloodEvents, g_cosCopies, g_cosPlain,
				g_cosNoClass, g_cosNoOriginator, g_cosOtherMap, g_cosBadState, g_cosCatchUpTicks, g_cosSoundsHeld,
				ghost_TopClasses(g_cosByClass).GetChars());
			g_cosEvents = g_cosBloodEvents = g_cosCopies = g_cosPlain = 0;
			g_cosNoClass = g_cosNoOriginator = g_cosOtherMap = g_cosBadState = g_cosCatchUpTicks = 0;
			g_cosSoundsHeld = 0;
		}

		// Mirror spawns: what ran, and how each record was judged.
		if ((g_msActions > 0) || (g_msJump + g_msResync + g_msSeed + g_msHard + g_msUnnamed > 0))
		{
			const FString top = ghost_TopClasses(g_msByClass);
			Printf("[snap] mirror spawns: %u actions, %u spawned, %u tics owed | records: %u jump, %u resync,"
				" %u held, %u seed, %u behind, %u undecided, %u hard, %u unnamed | %u rings | top:%s\n",
				g_msActions, g_msSpawned, g_msOwedTics, g_msJump, g_msResync, g_msHold, g_msSeed,
				g_msBehind, g_msUndecided, g_msHard, g_msUnnamed, g_msRings.CountUsed(), top.GetChars());
			g_msActions = g_msSpawned = g_msOwedTics = 0;
			g_msJump = g_msResync = g_msHold = g_msSeed = g_msBehind = g_msUndecided = g_msHard = g_msUnnamed = 0;
		}
		if (g_tgtAtEnter + g_tgtLate + g_tgtNever + g_tgtNoneMissile + g_tgtNoneOther + g_tgtNoneUnknown > 0)
		{
			const FString top = ghost_TopClasses(g_tgtNoneByClass);
			Printf("[snap] missile target: resolved at enter %u, late %u, never %u"
				" | A_CustomMissile with no target that would spawn: missile caster %u, other %u, unknown %u | top:%s\n",
				g_tgtAtEnter, g_tgtLate, g_tgtNever, g_tgtNoneMissile, g_tgtNoneOther, g_tgtNoneUnknown, top.GetChars());
			g_tgtAtEnter = g_tgtLate = g_tgtNever = g_tgtNoneMissile = g_tgtNoneOther = g_tgtNoneUnknown = 0;
			g_tgtNoneByClass.Clear();
		}

		// Effects on the wire against the mirror's, which P_RunEffects reads.
		unsigned int mirrorFx = 0;
		if (g_mirror != NULL)
		{
			for (unsigned int k = 0; k < g_mirrorIds.Size(); ++k)
			{
				const AActor * pM = (const AActor*)g_mirror[g_mirrorIds[k]];
				if ((pM != NULL) && (pM->effects != 0))
					++mirrorFx;
			}
		}
		Printf("[snap] effects: %u ghosts carry effects | %u mirrors nonzero"
			" | %u mirrors total\n",
			GhostsWithEffects(), mirrorFx, g_mirrorCount);

		Printf("[snap] hide: hidden %u"
			" | tick skipped %.0f/t ran %.0f/t\n",
			g_hidCount,
			(g_frameTicsRun > 0) ? (double)g_tickSkipped / (double)g_frameTicsRun : 0.0,
			(g_frameTicsRun > 0) ? (double)g_tickRan / (double)g_frameTicsRun : 0.0);

		// Worst single-frame rendered position jumps, with the sampler exit
		// that produced each one.
		{
			SnapJumpInfo jl[5];
			const unsigned int nj = TakeJumpLog(jl, 5);
			if (nj > 0)
			{
				FString line;
				for (unsigned int i = 0; i < nj; ++i)
				{
					const PClass* pT = NETWORK_GetClassFromIdentification((unsigned short)jl[i].classId);
					// The span since this ghost was rendered and its records.
					line.AppendFormat("%s%u:%s %uu %s pos %dt/%ums recs %u",
						(i > 0) ? " | " : "",
						jl[i].id, (pT != NULL) ? pT->TypeName.GetChars() : "?",
						jl[i].units, jl[i].branch, jl[i].gapTics,
						jl[i].posGapMS, jl[i].recs);
				}
				Printf("[snap] jumps: %s\n", line.GetChars());
			}

			// Outside the jump-log gate, as both censuses reset on read.
			{
				unsigned int cChk = 0, cBad = 0, cId = 0;
				unsigned int cUns = 0, cUnsBad = 0, cLate = 0;
				ChecksumStats(cChk, cBad, cId, cUns, cUnsBad, cLate, true);
				if ((cChk + cUns + cLate) > 0)
				{
					// Two forms, so no divergence prints no id.
					if (cBad > 0)
						Printf("[snap] checksum: %u verified, %u DIVERGED"
							" | %u unsettled (%u failed) | %u late, not checked"
							" | first bad id %u\n", cChk, cBad, cUns, cUnsBad, cLate, cId);
					else
						Printf("[snap] checksum: %u verified, 0 DIVERGED"
							" | %u unsettled (%u failed) | %u late, not checked\n",
							cChk, cUns, cUnsBad, cLate);
				}
			}

			// Printed even when empty: a zero here is the finding, since it
			// says the link did not reorder.
			{
				unsigned int fOld = 0, fWorst = 0, fSame = 0;
				FrameOrderStats(fOld, fWorst, fSame, true);
				unsigned int sKept = 0, sHeld = 0, sLeave = 0, sEnter = 0;
				unsigned int sAbsent = 0, sAfter = 0;
				StampStats(sKept, sHeld, sLeave, sEnter, sAbsent, sAfter, true);
				// Stale beside its witness: without an older payload it can
				// only read zero.
				Printf("[snap] frame order: %u older (worst %u frames back)"
					" | %u same-frame chunks | %u mover records refused stale"
					" | %u from a previous map, %u from a map not yet loaded,"
					" %u before the join, %u too late to count, %u past the ack history,"
					" %u ack-state resets at a join, %u after a run past the history"
					" | %u mover records from before a reset"
					" | %u reliable packets requested from a snapshot"
					" | %u groups kept from a late record, %u held back from the display"
					" | %u late leaves, %u late enters, %u enters after a leave for an id"
					" not held, %u late updates for an absent id,"
					" %u updates for an absent id at or after its leave\n",
					fOld, fWorst, fSame, mvStale, MapStampStale(true),
					MapStampAhead(true), BeforeJoin(true), TooLate(true),
					PastHistory(true), AckResets(true), AckRecoveries(true),
					mvBeforeReset, tailRequested,
					sKept, sHeld, sLeave, sEnter, LateEntersUnheld(true), sAbsent, sAfter);

				// The server clock model on live arrivals: its tic period, and the
				// spread of arrivals around its fit, which is the arrival jitter.
				SnapClockStats ck;
				ClockStats(ck, true);
				char szClock[384];
				ClockDescribe(ck, szClock, sizeof(szClock));
				Printf("[snap] clock: %s\n", szClock);
			}

			// The attribution by population, `pos` the records with position.
			{
				unsigned int jc[CLENT_POP_COUNT][CLENT_INTERVAL_BUCKETS];
				unsigned int ju[CLENT_POP_COUNT][CLENT_INTERVAL_BUCKETS];
				unsigned int jtot[CLENT_POP_COUNT];
				unsigned int ih[CLENT_POP_COUNT][CLENT_INTERVAL_BUCKETS];
				unsigned int itot[CLENT_POP_COUNT], rtot[CLENT_POP_COUNT];
				JumpGapStats(jc, ju, jtot, true);
				IntervalStats(ih, itot, rtot, true);

				for (unsigned int p = 0; p < CLENT_POP_COUNT; ++p)
				{
					if ((rtot[p] == 0) && (itot[p] == 0) && (jtot[p] == 0))
						continue;

					FString recs, jumps;
					for (unsigned int b = 0; b < CLENT_INTERVAL_BUCKETS; ++b)
					{
						recs.AppendFormat("%s%s %.0f%%", (b > 0) ? " " : "",
							CLENT_IntervalNames[b], (itot[p] > 0)
							? 100.0 * (double)ih[p][b] / (double)itot[p] : 0.0);
					}

					unsigned int uSum = 0;
					for (unsigned int b = 0; b < CLENT_INTERVAL_BUCKETS; ++b)
					{
						uSum += ju[p][b];
						jumps.AppendFormat("%s%s %.0f%%", (b > 0) ? " " : "",
							CLENT_IntervalNames[b], (jtot[p] > 0)
							? 100.0 * (double)jc[p][b] / (double)jtot[p] : 0.0);
					}

					Printf("[snap] pop %s: recs %u, pos %u (%s) | jumps64 %u (%s) avg %uu\n",
						CLENT_PopNames[p], rtot[p], itot[p], recs.GetChars(),
						jtot[p], jumps.GetChars(),
						(jtot[p] > 0) ? (uSum / jtot[p]) : 0u);
				}
			}
		}

		// High depth with low per-tic cost is a catch-up spiral, which a cap
		// fixes; the reverse is not.
		Printf("[snap] predict: %.1f calls/f depth avg %.1f max %llu | think %.2f/f move %.2f/f\n",
			(double)g_predCalls * inv,
			(g_predCalls > 0) ? (double)g_predTicsSum / (double)g_predCalls : 0.0,
			(unsigned long long)g_predTicsMax,
			ghost_RawToMS(g_predThinkRaw) * inv,
			ghost_RawToMS(g_predMoveRaw) * inv);

		// Percentages over the active population, heldnew printed raw.
		{
			SnapSampleCensus sc;
			SampleCensus(sc, true);
			const double active = (double)(sc.held + sc.heldMoving +sc.extrapolated + sc.jumped);
			Printf("[snap] sampler: active %.0f/f | held %.0f/f (%.0f%%, moving %.0f/f) "
				"extrap %.0f/f (velocity %.1f/f: %.1f/f at %dms cap, %.1f/f unproven missile,"
				" %.1f/f x/y in cell, %.1f/f at its edge)"
				" jump %.0f/f | heldnew %.0f/f (settled)\n",
				active * inv,
				(double)(sc.held + sc.heldMoving) * inv,
				(active > 0.0) ? 100.0 * (sc.held + sc.heldMoving) / active : 0.0,
				(double)sc.heldMoving * inv,
				(double)sc.extrapolated * inv,
				(double)sc.extrapVel * inv, (double)sc.extrapVelCapped * inv,
				(int)CLENT_VELEXTRAP_MS, (double)sc.extrapVelMissile * inv,
				(double)sc.extrapVelCell * inv, (double)sc.extrapVelCellEdge * inv,
				(double)sc.jumped * inv,
				(double)sc.heldNew* inv);

			// The age of each entity's newest record, dated as ageMS is.
			Printf("[snap] age: <80ms %.0f/f | <150 %.0f/f | <300 %.0f/f | <500 %.0f/f | 500+ %.0f/f\n",
				(double)sc.delayBucket[0] * inv, (double)sc.delayBucket[1] * inv,
				(double)sc.delayBucket[2] * inv, (double)sc.delayBucket[3] * inv,
				(double)sc.delayBucket[4] * inv);

			// What heldmove holds, top classes per side, still-moving buckets.
			{
				static SnapHeldMoveClass hm[512];
				unsigned int over[CLENT_HM_COUNT], players = 0;
				const unsigned int nc = HeldMoveCensus(hm, 512, over, players, true);
				unsigned int tot[CLENT_HM_COUNT], mon[CLENT_HM_COUNT];
				for (int b = 0; b < CLENT_HM_COUNT; ++b)
				{
					tot[b] = over[b];
					mon[b] = 0;
				}
				bool bMonster[512];
				for (unsigned int i = 0; i < nc; ++i)
				{
					const PClass* pT = NETWORK_GetClassFromIdentification(hm[i].classId);
					bMonster[i] = (pT != NULL) && (pT->Defaults != NULL)
						&& ((((AActor*)pT->Defaults)->flags3 & MF3_ISMONSTER) != 0);
					for (int b = 0; b < CLENT_HM_COUNT; ++b)
					{
						tot[b] += hm[i].n[b];
						if (bMonster[i])
							mon[b] += hm[i].n[b];
					}
				}
				unsigned int any = players;
				for (int b = 0; b < CLENT_HM_COUNT; ++b)
					any |= tot[b];
				if (any != 0)
				{
					// Shares are of heldmove, as the design reads them.
					const double hmAll = (sc.heldMoving > 0) ? (double)sc.heldMoving : 1.0;
					Printf("[snap] held moving: wire velocity %.1f/f %.0f%% (%u%% monster),"
						" position only %.1f/f %.0f%% (%u%% monster),"
						" missile unproven %.1f/f %.0f%%, no position for %d tics %.1f/f %.0f%%"
						" | players %.1f/f %.0f%%\n",
						tot[CLENT_HM_VEL] * inv, 100.0 * tot[CLENT_HM_VEL] / hmAll,
						tot[CLENT_HM_VEL] ? 100u * mon[CLENT_HM_VEL] / tot[CLENT_HM_VEL] : 0u,
						tot[CLENT_HM_POSONLY] * inv, 100.0 * tot[CLENT_HM_POSONLY] / hmAll,
						tot[CLENT_HM_POSONLY] ? 100u * mon[CLENT_HM_POSONLY] / tot[CLENT_HM_POSONLY] : 0u,
						tot[CLENT_HM_UNPROVEN] * inv, 100.0 * tot[CLENT_HM_UNPROVEN] / hmAll,
						CLENT_HM_REST_TICS, tot[CLENT_HM_RESTED] * inv,
						100.0 * tot[CLENT_HM_RESTED] / hmAll,
						players * inv, 100.0 * players / hmAll);

					// Top four with a wire velocity, and top four non-monsters
					// moving by position alone.
					char buf[2][256];
					for (int side = 0; side < 2; ++side)
					{
						unsigned int at = 0, shown[4];
						unsigned int nShown = 0;
						buf[side][0] = '\0';
						for (int pick = 0; pick < 4; ++pick)
						{
							int best = -1;
							unsigned int bestN = 0;
							for (unsigned int i = 0; i < nc; ++i)
							{
								const unsigned int v = side
									? ( bMonster[i] ? 0 : hm[i].n[CLENT_HM_POSONLY] )
									: hm[i].n[CLENT_HM_VEL];
								bool bTaken = false;
								for (unsigned int s = 0; s < nShown; ++s)
									bTaken |= ( shown[s] == i );
								if (( bTaken == false ) && ( v > bestN ))
								{
									best = (int)i;
									bestN = v;
								}
							}
							if (best < 0)
								break;
							shown[nShown++] = (unsigned int)best;
							const PClass* pT = NETWORK_GetClassFromIdentification(hm[best].classId);
							const int w = mysnprintf(buf[side] + at, sizeof(buf[side]) - at, " %s %.1f",
								pT ? pT->TypeName.GetChars() : "?", bestN * inv);
							if ((w > 0) && (at + w < sizeof(buf[side])))
								at += w;
						}
					}
					Printf("[snap] held moving, top:%s%s | position only, not monsters:%s\n",
						buf[0][0] ? "" : " none", buf[0], buf[1][0] ? buf[1] : " none");
				}
			}

			// Classification churn over the update counts, window totals.
			if ((sc.ballisticUpdates | sc.steeringUpdates | sc.classEnded | sc.classBegan | sc.classChanged) != 0)
			{
				Printf("[snap] ballistic: %u updates ballistic, %u steering"
					" | began %u ended %u | CHANGED IN FLIGHT %u\n",
					sc.ballisticUpdates, sc.steeringUpdates,
					sc.classBegan, sc.classEnded, sc.classChanged);
			}
		}
	}
	// The local player's own projectiles, apart from the monster-fired ones.
	{
		unsigned int fh[CLENT_FIRE_BUCKETS], fs = 0, fw = 0, fu = 0;
		unsigned int jn[4];
		double ja[4], jw[4];
		LocalFireStats(fh, fs, fw, fu, ja, jn, jw);
		if ((fs != 0) || (fu != 0))
		{
			char jbuf[160];
			unsigned int jat = 0;
			jbuf[0] = '\0';
			for (unsigned int k = 0; k < 4; ++k)
			{
				if (jat >= sizeof(jbuf) - 1)
					break;
				// The SAMPLE COUNT is printed, so an ordinal's figure says
				// whether it came from two shots or fifty.
				const int w = mysnprintf(jbuf + jat, sizeof(jbuf) - jat,
					" #%u %.0f/%.0f u/t x%u", k + 1,
					(jn[k] > 0) ? (ja[k] / (double)jn[k]) : 0.0,
					jw[k], jn[k]);
				if (w > 0)
					jat += (unsigned int)w;
			}
			const double finv = (fs > 0) ? (100.0 / (double)fs) : 0.0;
			Printf("[snap] own fire: %u shots | to first draw <35ms %.0f%% "
				"<70 %.0f%% <105 %.0f%% <175 %.0f%% 175+ %.0f%% (worst %u ms)"
				" | speed by record (mean/worst)%s | %u unmatched\n",
				fs, fh[0] * finv, fh[1] * finv, fh[2] * finv,
				fh[3] * finv, fh[4] * finv, fw, jbuf, fu);
		}
	}

	// Reported whether or not it is on, so "off" and "on but reaching
	// nothing" are distinguishable.
	if (g_ghostCensusFrames > 0)
	{
		// `solid` a per-frame mean, the other two window totals.
		const double cinv = 1.0 / (double)g_ghostCensusFrames;
		Printf("[snap] collide: %s | %.1f solid/frame (max %u)"
			" | %llu transitions, %llu corpse-released over %u frames | dist %d\n",
			fua_csnap_collide ? "on" : "off",
			(double)(QWORD)g_ghostSolidAccum * cinv, g_ghostSolidMax,
			(unsigned long long)g_ghostSolidChangeAccum,
			(unsigned long long)g_ghostSolidCorpseAccum,
			g_ghostCensusFrames,
			(int)fua_csnap_collide_dist);
	}

	// Phase 5 movers every window, read as snap_movers.md says.
	{
		unsigned int mvRecords = 0, mvNoSink = 0, mvBad = 0;
		SnapMoverClientStats mv;
		memset(&mv, 0, sizeof(mv));
		ClientMoverStats(mvRecords, mvNoSink, mvBad, true);
		MoverClientStats(mv, true);

		// Only the classes that corrected something, so the line does not
		// grow a zero column per class.
		char szRest[256];
		size_t nRest = 0;
		szRest[0] = '\0';
		for (int i = 0; i < SNAPMV_STATCLASSES; i++)
		{
			if ((mv.restPlane[i] == 0) || (nRest + 1 >= sizeof(szRest)))
				continue;
			mysnprintf(szRest + nRest, sizeof(szRest) - nRest,
				" %s %u worst %u", ComputeMoverClassName((BYTE)i),
				mv.restPlane[i], mv.restWorst[i]);
			// strlen, not the return: mysnprintf reports the IDEAL length,
			// so accumulating it walks past the buffer on truncation.
			nRest = strlen(szRest);
		}
		if (nRest == 0)
			mysnprintf(szRest, sizeof(szRest), " none");

		char szGone[256];
		size_t nGone = 0;
		szGone[0] = '\0';
		for (int i = 0; i < SNAPMV_STATCLASSES; i++)
		{
			if ((mv.gonePlane[i] == 0) || (nGone + 1 >= sizeof(szGone)))
				continue;
			mysnprintf(szGone + nGone, sizeof(szGone) - nGone,
				" %s %u worst %u", ComputeMoverClassName((BYTE)i),
				mv.gonePlane[i], mv.goneWorst[i]);
			nGone = strlen(szGone);
		}
		if (nGone == 0)
			mysnprintf(szGone, sizeof(szGone), " none");

		Printf("[snap] movers: %u records (%u no sink, %u unreadable)"
			" | %u applied, %u gone (%u no local, %u of them moving the plane,"
			" %u superseded, %u with a plane stale),"
			" %u no level, %u unusable, %u stale"
			" | stale occupants evicted %u on create, %u on gone"
			" (%u by generation), %u for a waggle"
			" | vetoes overridden %u sector, %u attached (all movers)"
			" | %u plat crushes survived"
			" | plat waits: %u ended by record (%u left avg, %u worst),"
			" %u ran out, over %u tics counted down"
			" | door waits: %u ran out, %u ended by record within a tic,"
			" %u early (worst %u), over %u tics counted down"
			" | blocked %u records, %u tics held (%u on one plane),"
			" %u planes set (worst %u)"
			" | %u reversals derived"
			" | %u legacy calls suppressed"
			" | at-rest planes corrected:%s"
			" | on death:%s"
			" | %u plane verifications suppressed, %u on linked planes"
			" | %u midtex offsets suppressed"
			" | map resets %u (%u closed, %u abandoned%s), generation %u%s"
			" | reset hold: %u deferred (%u ahead of a reset), %u replayed,"
			" %u dropped, %u coalesced, %u narrowed, peak %u of %u,"
			" %u advanced (%u tics, worst %u), %u past the cap\n",
			mvRecords, mvNoSink, mvBad,
			mv.applied, mv.gone, mv.goneNoLocal, mv.goneUnheldPlane, mv.goneSuperseded,
			mv.gonePlanesStale,
			mv.noLevel, mv.unusable, mvStale,
			mv.evicted, mv.goneEvicted, mv.genEvicted, mv.waggleEvicted,
			mv.noblock, mv.attached, mv.crushed,
			mv.waitEnded,
			mv.waitEnded ? ( mv.waitResid / mv.waitEnded ) : 0u,
			mv.waitWorst, mv.waitExpired, mv.waitTicks,
			mv.doorWaitExpired, mv.doorWaitEnded, mv.doorWaitEarly,
			mv.doorWaitEarlyWorst, mv.doorWaitTicks,
			mv.blockedRecv, mv.held, mv.heldOnePlane, mv.stallPlane, mv.stallWorst,
			mv.derivedReversals,
			mv.legacySuppressed,
			szRest, szGone, mv.planeSuppressed, mv.linkSuppressed,
			mv.midtexSuppressed, mv.resetsOpened, mv.resetsClosed,
			mv.resetsAbandoned,
			mv.bResetOpen ? ", one OPEN" : "",
			mv.resetCount, mv.bHaveResetCount ? "" : " (none known)",
			mv.holdIn, mv.holdAhead, mv.holdReplayed, mv.holdDropped,
			mv.holdCoalesced, mv.holdNarrowed, mv.holdPeak, mv.holdBound,
			mv.holdAdvanced, mv.holdAdvanceTics, mv.holdAdvanceWorst,
			mv.holdAdvanceCapped);

		// The mover gate (7.1), what each flip dropped by class.
		SnapMoverGateStats gt;
		memset(&gt, 0, sizeof(gt));
		ClientMoverGateStats(gt, true);
		char szDrop[2][128];
		for (int k = 0; k < 2; ++k)
		{
			size_t nDrop = 0;
			szDrop[k][0] = '\0';
			for (int i = 0; i < SNAPMV_STATCLASSES; ++i)
			{
				if ((gt.dropped[k][i] == 0) || (nDrop + 1 >= sizeof(szDrop[k])))
					continue;
				mysnprintf(szDrop[k] + nDrop, sizeof(szDrop[k]) - nDrop, " %s %u",
					ComputeMoverClassName((BYTE)i), gt.dropped[k][i]);
				nDrop = strlen(szDrop[k]);
			}
			if (nDrop == 0)
				mysnprintf(szDrop[k], sizeof(szDrop[k]), " none");
		}
		Printf("[snap] mover gate: legacy off %d | dropped at the gate: legacy-made%s,"
			" record-made%s | %u kept poly records applied | %u records refused"
			" below the gate floor (floor %d)\n",
			gt.bGate ? 1 : 0, szDrop[0], szDrop[1], gt.keptApplied, gt.belowFloor,
			gt.floor);

		// Polyobjects (7.1), read as snap_polyobj.md says.
		SnapPolyClientStats po;
		memset(&po, 0, sizeof(po));
		PolyClientStats(po, true);
		Printf("[snap] polys: %u records, %u actions created, %u evicted,"
			" %u gone (%u no local), %u stale, %u unusable,"
			" %u ignored with legacy live"
			" | tics held %u blocked, %u done"
			" | axes set: %u first, %u at rest, %u blocked, %u gone"
			" (worst %.2f units, %.2f deg)"
			" | resync %u records, %u corrected (worst %.2f deg)"
			" | %u legacy position writes suppressed\n",
			po.records, po.created, po.evicted, po.gone, po.goneNoLocal,
			po.stale, po.unusable, po.ungated, po.held, po.done,
			po.set[SNAPPO_SET_FIRST], po.set[SNAPPO_SET_REST],
			po.set[SNAPPO_SET_BLOCKED], po.set[SNAPPO_SET_GONE],
			po.setWorstMove / 65536.0, po.setWorstTurn * ( 360.0 / 4294967296.0 ),
			po.resyncs, po.set[SNAPPO_SET_RESYNC],
			po.resyncWorst * ( 360.0 / 4294967296.0 ),
			po.suppressed);

		// DoScroller and DoPusher (7), read as snap_movers.md says.
		SnapScrollPushClientStats sp;
		memset(&sp, 0, sizeof(sp));
		SpecialClientStats(sp, true);
		Printf("[snap] scrollers: %u joined (%u controlled, %u accelerative,"
			" %u moving) | point pushers: %u joined (%u no source),"
			" %u resolved (%u mirror), %u again, %u unresolved tics, %u gone"
			" | full update replaced %u scrollers, %u pushers, %u waggles\n",
			sp.scrollers, sp.controlled, sp.accelerative, sp.moving,
			sp.points, sp.noSource, sp.resolved, sp.resolvedMirror,
			sp.reresolved, sp.unresolved, sp.gone, sp.replacedScrollers,
			sp.replacedPushers, sp.replacedWaggles);

		// The join's own command, where every record must land on a mover.
		Printf("[snap] special state: %u chunks | %u scroller + %u pusher records applied"
			" | %u echo mismatch (%u matched by search, %u unmatched)%s\n",
			sp.stateChunks, sp.stateScrollers, sp.statePushers,
			sp.stateEchoMismatch, sp.stateFallback, sp.stateUnmatched,
			( sp.stateUnmatched > 0 ) ? "  ** UNACCOUNTED **" : "");
	}

	g_ghostNearAccum = g_ghostMidAccum = g_ghostFarAccum = 0;
	g_ghostRelinkAccum = g_ghostCreateAccum = g_ghostRecreateAccum = 0;
	g_ghostSolidAccum = g_ghostSolidChangeAccum = g_ghostSolidCorpseAccum = 0;
	g_ghostSolidMax = 0;
	g_gcAllocSeen = false;
	g_mirrorLookupOk = g_mirrorLookupEmpty = 0;
	g_mirrorLookupBadId = g_mirrorLookupNoTable = 0;
	g_tickSkipped = 0;
	g_tickRan = 0;
	g_predCalls = g_predTicsSum = g_predTicsMax = 0;
	g_predThinkRaw = g_predMoveRaw = 0;
	g_ghostSelfRaw = 0;
	g_frameSimRaw = 0;
	g_frameRenderRaw = 0;
	g_frameTicsRun = 0;
	g_ticThinkersRaw = 0;
	g_ticEffectsRaw = 0;
	g_applyDeltaRaw = 0;
	g_gtickerRaw = 0;
	g_netupdateRaw = 0;
	g_ghostCensusFrames = 0;
	g_ghostNearMax = 0;
	g_ghostRelinkMax = 0;
}

void zx::netsnapshot::GhostReportNow()
{
	// Most lines are per frame and print only over at least one.
	if (g_ghostCensusFrames == 0)
		g_ghostCensusFrames = 1;
	ghost_Report();
	// The report clears these only beside the localanim counts, which a fuzz
	// run leaves at zero.
	ClientEventStatsReset();
	MissileStatsReset();
	SnapBlindPredict bp;
	BlindPredict(bp, true);
}

//*****************************************************************************
//	CONSOLE COMMANDS

// What the client holds for the ghosts nearest the camera, or every ghost of a
// class; `age` (ms since the newest state) is read as cl_ghostactor.md says.
CCMD(fua_csnap_ghostinfo)
{
	// A class filter lists every ghost of that class, a duplicate's question.
	unsigned int want = 8;
	const char* pszFilter = NULL;
	if (argv.argc() > 1)
	{
		const int n = atoi(argv[1]);
		if ((n > 0) || (argv[1][0] == '0'))
			want = (n > 0) ? (unsigned int)n : 8;
		else
		{
			pszFilter = argv[1];
			want = 64;
		}
	}

	if ((consoleplayer < 0) || (consoleplayer >= MAXPLAYERS))
		return;

	const AActor* pCam = players[consoleplayer].camera;
	if (pCam == NULL)
		pCam = players[consoleplayer].mo;
	if (pCam == NULL)
	{
		Printf("fua_csnap_ghostinfo: no viewpoint.\n");
		return;
	}

	unsigned int count = 0;
	const unsigned int* pIds = PresentIds(count);
	if (count == 0)
	{
		Printf("fua_csnap_ghostinfo: no ghosts (present=0).\n");
		return;
	}

	const unsigned int nowMS = I_MSTime();

	// Selection sort over the nearest `want`: this is a console command, and
	// keeping it allocation-free matters more than the asymptotics.
	Printf("fua_csnap_ghostinfo: %u ghosts present, %u mirrors, localanim level %d.%s%s\n",
		count, g_mirrorCount, (int)fua_csnap_localanim,
		(pszFilter != NULL) ? " class=" : " Nearest.",
		(pszFilter != NULL) ? pszFilter : "");
	Printf("   id    class                 dist    age   window  wire   shown     fx  mfx        x       y\n");

	TArray<unsigned int> shown;
	for (unsigned int pick = 0; (pick < want) && (pick < count); ++pick)
	{
		double bestDist = 0.0;
		unsigned int bestId = 0;
		bool found = false;

		for (unsigned int i = 0; i < count; ++i)
		{
			const unsigned int id = pIds[i];

			bool already = false;
			for (unsigned int s = 0; s < shown.Size(); ++s)
			{
				if (shown[s] == id)
				{
					already = true;
					break;
				}
			}
			if (already)
				continue;

			SnapGhostDebug dbg;
			if (GhostDebug(id, dbg) == false)
				continue;

			if (pszFilter != NULL)
			{
				const PClass* pT = NETWORK_GetClassFromIdentification(dbg.classId);
				if ((pT == NULL) || (stricmp(pT->TypeName.GetChars(), pszFilter) != 0))
					continue;
			}

			const double dx = (double)(dbg.x - pCam->x) / 65536.0;
			const double dy = (double)(dbg.y - pCam->y) / 65536.0;
			const double d = sqrt(dx * dx + dy * dy);

			if ((found == false) || (d < bestDist))
			{
				bestDist = d;
				bestId = id;
				found = true;
			}
		}

		if (found == false)
			break;
		shown.Push(bestId);

		SnapGhostDebug dbg;
		if (GhostDebug(bestId, dbg) == false)
			continue;

		const PClass* pType = NETWORK_GetClassFromIdentification(dbg.classId);
		const char* pszName = (pType != NULL) ? pType->TypeName.GetChars() : "<unknown>";

		// Milliseconds since the newest state arrived, and how far apart the
		// two newest states arrived.
		const unsigned int age = (nowMS > dbg.nextMS) ? (nowMS - dbg.nextMS) : 0;
		const unsigned int window = (dbg.nextMS > dbg.prevMS) ? (dbg.nextMS - dbg.prevMS) : 0;

		// The frame the server reported beside the one the mirror shows.
		// [Kaori-Chani] From the state the wire carried: spriteframe is filled by the
		// capture for its own dumps and is 0 here, which read as TNT1 A for everything.
		char wire[16], shown[16];
		const FState * pWire = (pType != NULL) ? StateFromQState(pType, dbg.qstate) : NULL;
		if (pWire != NULL)
			ghost_DescribeFrame((WORD)pWire->sprite, (BYTE)pWire->GetFrame(), wire, sizeof(wire));
		else
			strcpy(wire, (dbg.qstate == SNAP_STATE_UNKNOWN) ? "other" : "?");

		const AActor* pMirror = (g_mirror != NULL) ? (const AActor*)g_mirror[bestId] : NULL;
		if (pMirror != NULL)
			ghost_DescribeFrame(pMirror->sprite, pMirror->frame, shown, sizeof(shown));
		else
			strcpy(shown, "-");

		// Position tells a duplicate from one drawn twice; fx is the wire's
		// effects byte, mfx the mirror's.
		Printf("%5u    %-20s %6.0f %6ums %6ums  %-6s %-8s  0x%02x 0x%02x %7.0f %7.0f\n",
			bestId, pszName, bestDist, age, window, wire, shown,
			(unsigned int)dbg.effects,
			(pMirror != NULL) ? (unsigned int)pMirror->effects : 0u,
			(double)dbg.x / 65536.0, (double)dbg.y / 65536.0);
	}
}
