//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: capture.cpp
//
// Description: The server's snapshot tick: the world capture and its
// censuses, transmission to each client, legacy suppression, cosmetic classes
// and events, snapshot sounds, mover capture, and the stats report. See
// capture.h.
//
//-----------------------------------------------------------------------------

#include <string.h>
#include <math.h>		// atan2, for the straightness census
#include <stdlib.h>
#include <stdio.h>

#include <zstd.h>

#include "doomdef.h"
#include "templates.h"
#include "actor.h"
#include "r_data/renderstyle.h"
#include "dthinker.h"
#include "doomstat.h"
#include "d_player.h"
#include "d_netinf.h"		// CLIENTFLAGS_CLIENTSIDEPUFFS, for the puff shooter skip
// AWeapon is only forward-declared in d_player.h; the psprite capture needs
// its class to resolve a state index.
#include "a_pickups.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "r_state.h"
#include "c_console.h"
#include "network.h"
#include "i_system.h"
#include "s_sound.h"
// [Kaori-Chani] The time base: declarations here, implementations with posix/sdl/ and win32/
// in step 3, since the counter is platform-specific and these declarations are not.
#include "i_tictime.h"
#include "tarray.h"
#include "p_effect.h"
#include "features/net-snapshot/format.h"
#include "features/net-snapshot/computation/law_compute.h"
#include "features/net-snapshot/computation/join_compute.h"
#include "features/net-snapshot/computation/mover_compute.h"
#include "features/net-snapshot/computation/budget_compute.h"
#include "features/net-snapshot/clienttrack.h"
#include "sv_main.h"
#include "sv_commands.h"
#include "s_sndseq.h"	// [Kaori-Chani] SN_CheckSequence, for the probe below
#include "network/netcommand.h"
#include "features/net-snapshot/capture.h"
#include "features/net-snapshot/selftest.h"
#include "r_data/r_translate.h"	// TRANSLATION_SHIFT, NUM_TRANSLATION_TABLES
#include "p_spec.h"				// DPlat, DDoor, DScroller, DPusher, ...
#include "p_local.h"			// DPolyAction and its subclasses
#include "po_man.h"				// FPolyObj, for the polyobject dump
#include "dsectoreffect.h"	// DSectorEffect, the base the census walks
#include "p_tags.h"			// tagManager: a sector carries a list of tags here, not one short
#include "features/net-snapshot/entities.h"	// NewestGameTic, to align a client reading
#include "features/net-snapshot/ghostactor.h"	// cosmetic events: the client side of the hooks below
#include "thingdef/thingdef.h"		// cosmetic classes: action functions and their parameters
#include "thingdef/thingdef_exp.h"
#include "p_acs.h"					// cosmetic classes: a script's CLIENTSIDE flag

using namespace zx::netsnapshot;

//*****************************************************************************
//	CONSOLE VARIABLES

CVAR(Bool, fua_snap, false, CVAR_NOSETBYACS)
CVAR(Int, fua_snap_stats, 0, CVAR_NOSETBYACS)

// [Kaori-Chani] The gate's two directions. Off is the fallback if the snapshot
// misbehaves, so it takes effect in the tick it is set; on waits for the next
// map, where every client is sent a frame and a sound table anyway, so none is
// left applying records against a frame it never got.
static bool g_snapGateOn = false;
static bool g_snapGateAsked = false;	// an on, waiting for that map

static bool snap_GateOn()
{
	return g_snapGateOn;
}

// Before any other work in the tick, so an off reaches the mover gate and the
// resets below it in the same tick.
static void snap_GateTick()
{
	if (fua_snap == false)
	{
		g_snapGateOn = false;
		g_snapGateAsked = false;
		return;
	}
	if (g_snapGateOn || g_snapGateAsked)
		return;
	g_snapGateAsked = true;
	Printf("fua_snap takes effect at the next map.\n");
}

// At the teardown in P_SetupLevel, which runs before the next map's data loads
// and before its clients are sent anything.
static void snap_GateLatch()
{
	g_snapGateOn = fua_snap;
	g_snapGateAsked = false;
}

// The per-frame payload budget in uncompressed bytes, across datagrams, the
// packet ceiling applying to each chunk; archived, as a calibrated parameter.
CUSTOM_CVAR(Int, fua_snap_maxbytes, 4500, CVAR_ARCHIVE | CVAR_NOSETBYACS)
{
	if (self < 64)
	{
		self = 64;
		return;
	}

	// A budget over one datagram is allowed; the count it implies is reported.
	if ((NETWORK_GetState() == NETSTATE_SERVER) && (gamestate != GS_STARTUP))
	{
		const int room = (int)SERVER_GetMaxPacketSize() - SNAP_PACKET_OVERHEAD;
		if ((room > 0) && ((int)self > room))
			Printf("fua_snap_maxbytes %d spans up to %d datagrams at "
				"sv_maxpacketsize %u.\n",
				(int)self, ((int)self + room - 1) / room,
				(unsigned int)SERVER_GetMaxPacketSize());
	}
}

CVAR(Int, fua_snap_zstd_level, 3, CVAR_NOSETBYACS)	// lower to 1 if CPU-bound during stress runs

CVAR(Bool, fua_snap_send, false, CVAR_NOSETBYACS)	// actually transmit deltas to clients
// The interest cull radius, past which an entity gets a bounded refresh.
CVAR(Int, fua_snap_cull_dist, 4096, CVAR_ARCHIVE | CVAR_NOSETBYACS)

// The full cone, in degrees, outside which the cull may demote an entity, wider
// than any FOV a client renders since the server is not told it.
CUSTOM_CVAR(Int, fua_snap_cull_fov, 200, CVAR_NOSETBYACS)
{
	if (self < 1)
		self = 1;
	UpdateCullFov((int)self);
}

// The radius inside which a missile goes to the missile band, apart from
// fua_snap_cull_dist so a tighter cull does not starve cross-map projectiles.
CVAR(Int, fua_snap_missile_dist, 4096, CVAR_NOSETBYACS)

// The share of each client's budget reserved for the bands below d0, split by
// population, every record it buys coming out of d0's.
CVAR(Int, fua_snap_bandreserve, 15, CVAR_ARCHIVE | CVAR_NOSETBYACS)

// Defined in clienttrack.cpp; read here only to report them.
EXTERN_CVAR(Int, fua_snap_missile_predict)
EXTERN_CVAR(Bool, fua_snap_appear_predict)
EXTERN_CVAR(Int, fua_snap_animref_interval)
EXTERN_CVAR(Bool, fua_snap_wirecensus)

// Defined further down, with the mover work it gates; the stats report reads
// it above that point.
EXTERN_CVAR(Bool, fua_snap_movers)

// Frames a client must have believed in an entity before its sounds go attached
// rather than positional (18: about 510 ms; 0 as soon as it is believed).
CVAR(Int, fua_snap_sound_entergrace, 18, CVAR_ARCHIVE | CVAR_NOSETBYACS)

// The classes fua_snap_nonetid_classes set NETFL_NONETID on, so a change to it
// clears exactly those and never one that shipped with +NONETID.
static TArray<const PClass*> g_forcedNoNetID;

static void snap_ApplyNoNetIDClasses(const char* pszList)
{
	// Undo the previous application first; the cvar is a complete statement of
	// the set, not an addition to it.
	for (unsigned int i = 0; i < g_forcedNoNetID.Size(); ++i)
	{
		AActor* pDef = GetDefaultByType(g_forcedNoNetID[i]);
		if (pDef != NULL)
			pDef->NetworkFlags &= ~NETFL_NONETID;
	}
	g_forcedNoNetID.Clear();

	if ((pszList == NULL) || (*pszList == '\0'))
		return;

	// Comma- or space-separated, parsed on a change or map load.
	FString token;
	for (const char* p = pszList; ; ++p)
	{
		if ((*p != '\0') && (*p != ',') && (*p != ' ') && (*p != '\t'))
		{
			token += *p;
			continue;
		}
		if (token.Len() > 0)
		{
			const PClass* pType = PClass::FindClass(token.GetChars());
			if ((pType == NULL) || (pType->IsDescendantOf(RUNTIME_CLASS(AActor)) == false))
			{
				// Only once a map has loaded, since no class resolves before
				// LoadActors; MapChanged applies it then.
				if (gamestate == GS_LEVEL)
					Printf("fua_snap_nonetid_classes: unknown actor class '%s'\n", token.GetChars());
			}
			else
			{
				AActor* pDef = GetDefaultByType(pType);
				if ((pDef != NULL) && ((pDef->NetworkFlags & NETFL_NONETID) == 0))
				{
					pDef->NetworkFlags |= NETFL_NONETID;
					g_forcedNoNetID.Push(pType);
				}
			}
			token = "";
		}
		if (*p == '\0')
			break;
	}
}

CUSTOM_CVAR(String, fua_snap_nonetid_classes, "RevenantTracerSmoke", CVAR_ARCHIVE | CVAR_NOSETBYACS)
{
	snap_ApplyNoNetIDClasses(self);
}

// 0 every enter to ENTER, 1 always distance-grade, 2 grade only while the
// client fills its baseline, leaving a fight's projectile enters fast.
CVAR(Int, fua_snap_entergrade, 2, CVAR_NOSETBYACS)

//*****************************************************************************
//	SERVER-SIDE CAPTURE (format definitions live in format.h)

// An actor's render style as a legacy ERenderStyle index (snap_format.md).
static BYTE snap_LegacyStyleIndex(const FRenderStyle& style)
{
	for (int i = 0; i < STYLE_Count; ++i)
	{
		if (LegacyRenderStyles[i].AsDWORD == style.AsDWORD)
			return (BYTE)i;
	}
	// Custom style: approximate rather than send something undrawable.
	return (BYTE)STYLE_Normal;
}

// Whether this entity's heading has held long enough to extrapolate from,
// defined with the straightness tracker below.
static bool snap_IsStraight(unsigned int id);
// The same question for constant ACCELERATION, and declared here for the same
// reason.
static bool snap_IsGravStraight(unsigned int id);

// Captures classified as constant-acceleration, for the report.
static unsigned int g_gravClassified = 0;

// Missile state at the previous capture, and whether the id has detonated and
// is playing out its death states.
#define SNAP_TERMINAL_TICS	20		// covers an 8-16 tic death sequence

static BYTE * g_wasMissile = NULL;
static BYTE * g_terminalSeq = NULL;

// The shooter NetID of a spawner-class missile at the last capture, 0 for none,
// outside SnapEntityState since only an enter carries it.
static WORD * g_targetAtId = NULL;
static unsigned int g_targetEnters = 0, g_targetNoneEnters = 0;

// The class last seen at each NetID, a change meaning the id was recycled.
static WORD * g_lastClassAtId = NULL;

// Captured actors with a nonzero effects byte and the union of the bits, which
// the client's report should match.
static unsigned int	g_capEffectsActors = 0;
static BYTE			g_capEffectsBits = 0;

// The map's position frame, computed on first use after a map change, and its
// clamps in this report window.
static SnapFrame		g_mapFrame = FrameNarrow();
static bool				g_mapFrameDue = true;
static unsigned int		g_frameClamps = 0;

// The extent of the loaded map: vertices, sector heights at each line's ends,
// polyobject start spots and every actor, then the frame around it.
static void snap_ComputeFrame()
{
	long long lo[3], hi[3];
	for (unsigned int k = 0; k < 3; ++k)
	{
		lo[k] = 1LL << 62;
		hi[k] = -(1LL << 62);
	}
	for (int i = 0; i < numvertexes; ++i)
	{
		ComputeExtentTake(lo, hi, 0, vertexes[i].x);
		ComputeExtentTake(lo, hi, 1, vertexes[i].y);
	}
	for (int i = 0; i < numlines; ++i)
	{
		const line_t &l = lines[i];
		const sector_t * const sides[2] = { l.frontsector, l.backsector };
		for (unsigned int s = 0; s < 2; ++s)
		{
			if (sides[s] == NULL)
				continue;
			ComputeExtentTake(lo, hi, 2, sides[s]->floorplane.ZatPoint(l.v1));
			ComputeExtentTake(lo, hi, 2, sides[s]->floorplane.ZatPoint(l.v2));
			ComputeExtentTake(lo, hi, 2, sides[s]->ceilingplane.ZatPoint(l.v1));
			ComputeExtentTake(lo, hi, 2, sides[s]->ceilingplane.ZatPoint(l.v2));
		}
	}
	for (int i = 0; i < po_NumPolyobjs; ++i)
	{
		ComputeExtentTake(lo, hi, 0, polyobjs[i].StartSpot.x);
		ComputeExtentTake(lo, hi, 1, polyobjs[i].StartSpot.y);
	}
	TThinkerIterator<AActor> it;
	for (AActor *pActor = it.Next(); pActor != NULL; pActor = it.Next())
	{
		ComputeExtentTake(lo, hi, 0, pActor->x);
		ComputeExtentTake(lo, hi, 1, pActor->y);
		ComputeExtentTake(lo, hi, 2, pActor->z);
	}
	bool bOverflow = false;
	g_mapFrame = FrameFromExtent(lo, hi, (long long)FIXED_MIN >> FRACBITS,
		(long long)FIXED_MAX >> FRACBITS, &bOverflow);
	g_mapFrame.pClamped = &g_frameClamps;
	if (bOverflow)
		Printf("[snap] map wider than 16,777,215 units on an axis: positions past the frame clamp\n");
	const SnapFrame &f = g_mapFrame;
	if (((f.origin[0] | f.origin[1] | f.origin[2]) != 0)
		|| ((FrameExtra(f, 0) + FrameExtra(f, 1) + FrameExtra(f, 2)) != 0))
	{
		Printf("[snap] map frame origin %d %d %d, %u %u %u bytes an axis\n", f.origin[0],
			f.origin[1], f.origin[2], f.width[0], f.width[1], f.width[2]);
	}
}

const SnapFrame& zx::netsnapshot::ServerMapFrame()
{
	// Outside a level there is no extent to take, so the frame waits for one.
	// [Kaori-Chani] A server alone takes it, a client's arriving in SnapJoin and single
	// player sending nothing at all.
	if (g_mapFrameDue && (gamestate == GS_LEVEL) && (NETWORK_GetState() == NETSTATE_SERVER))
	{
		g_mapFrameDue = false;
		snap_ComputeFrame();
	}
	return g_mapFrame;
}

// Quantizes one actor (non-const, as TObjPtr's null test is) into a state.
static void snap_CaptureActor(AActor* pActor, SnapEntityState& out)
{
	out.id = pActor->NetID;
	out.classId = (WORD)pActor->GetClass()->getActorNetworkIndex();
	out.spriteframe = PackSpriteFrame(pActor->sprite, pActor->frame);
	out.qangle = QuantAngle(pActor->angle);
	// Wire precision, so the capture and the codec-round-tripped reference
	// describe the same value.
	out.qpitch = WirePitch(QuantPitch(pActor->pitch));
	const SnapFrame &frame = ServerMapFrame();
	out.qx = QuantPosIn(frame, 0, pActor->x);
	out.qy = QuantPosIn(frame, 1, pActor->y);
	out.qz = QuantPosIn(frame, 2, pActor->z);
	out.qvx = QuantVel(pActor->velx);
	out.qvy = QuantVel(pActor->vely);
	out.qvz = QuantVel(pActor->velz);

	// Fails the build if a translation table past the 32 the wire holds is
	// added, or if the type leaves bit 16.
	typedef char snap_assert_transtype[
		(NUM_TRANSLATION_TABLES <= (int)(SNAP_RS_TRANSTYPE_MASK + 1)) ? 1 : -1];
	typedef char snap_assert_transshift[
		(TRANSLATION_SHIFT == SNAP_RS_TRANS_SHIFT) ? 1 : -1];

	out.renderstate = PackRenderState(pActor->Translation,
		snap_LegacyStyleIndex(pActor->RenderStyle), pActor->alpha);

	// Tics remaining in the state, 255 for an infinite one or one long
	// enough that its own duration substitutes.
	out.qtics = ((pActor->tics < 0) || (pActor->tics > 254))
		? (BYTE)255 : (BYTE)pActor->tics;

	// Which state, not just which frame, as the owning class's depth in the
	// actor's ancestor chain and the offset in its table.
	out.qstate = QStateForState(pActor->GetClass(), pActor->state);

	// AActor::effects, since what PostBeginPlay sets never reaches a mirror.
	out.effects = PackEffects((DWORD)pActor->effects);

	// A spawner missile's shooter, for its enter.
	if (g_targetAtId != NULL)
	{
		g_targetAtId[out.id] = ((pActor->flags & MF_MISSILE) && (pActor->target != NULL)
			&& (pActor->target->NetID != 0) && ClassSpawns(pActor->GetClass()))
			? (WORD)pActor->target->NetID : (WORD)0;
	}

	if (out.effects != 0)
	{
		++g_capEffectsActors;
		g_capEffectsBits |= out.effects;
	}

	out.misc = 0;
	if (pActor->flags & MF_CORPSE)		out.misc |= SNAPMISC_CORPSE;
	if (pActor->renderflags & RF_INVISIBLE)	out.misc |= SNAPMISC_INVISIBLE;
	if (pActor->floorclip != 0)			out.misc |= SNAPMISC_FLOORCLIP;
	if (pActor->waterlevel != 0)
		out.misc |= (WORD)((pActor->waterlevel & 3) << SNAPMISC_WATERSHIFT);
	// The crouch value, sent since the client's integrator runs on a local
	// P_TryMove and a crouch decides whether a shot passes over.
	// [Kaori-Chani] QuantCrouch takes ints, since format.h includes no engine header, and neither
	// argument converts on its own now: crouchfactor is a 48.16 fixed_t, and FRACUNIT is one too,
	// being ((fixed_t)1<<FRACBITS) rather than the plain constant it reads as. Both raws are the
	// right things to hand over, because both sides of the comparison inside are 1/65536ths, and a
	// crouch lives between half a unit and one, so neither narrowing loses anything.
	if (pActor->player != NULL)
		out.misc |= (WORD)(QuantCrouch((int)pActor->player->crouchfactor, (int)FRACUNIT)
			<< SNAPMISC_CROUCH_SHIFT);

	// The missile bits come from measured straightness, not the class: several
	// straight tics earn them and one deviation loses them.

	if (pActor->flags & (MF_MISSILE | MF_SKULLFLY))
	{
		// Unproven when the wire velocity is 0, or a still missile flagged
		// ballistic freezes at the client's cap.
		if ((out.qvx | out.qvy | out.qvz) == 0)
		{
			out.misc |= SNAP_MISC_STEERING;
		}
		else if (snap_IsStraight(pActor->NetID))
		{
			// Constant velocity: extrapolate, and the server may suppress.
			out.misc |= SNAP_MISC_BALLISTIC;
		}
		else if (snap_IsGravStraight(pActor->NetID))
		{
			++g_gravClassified;
			// Constant acceleration: extrapolated with a gravity term but never
			// suppressed, since the client derives the acceleration.
			out.misc |= (SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING);
		}
		else
		{
			out.misc |= SNAP_MISC_STEERING;	// turning, or not yet proven
		}
	}
}


// The dirty predicate is DiffMask, shared with the per-client pass.

//*****************************************************************************
//	FRAME STATE AND STATISTICS

#define SNAP_SETTLE_TICS	TICRATE		// unchanged this long = "settled"

static bool				g_snapInitialized = false;
static SnapEntityState* g_prevState = NULL;		// indexed by NetID
static unsigned int	g_snapFrame = 0;
// Map loads on this server, on every SVC2_SNAPSHOT and SnapJoin, compared
// serially; a client accepts only its joined map's stamp.
static BYTE				g_snapMapStamp = 0;
// Map resets on this server across maps, on every SVC2_SNAPSHOT and
// DestroyAllSectorMovers; a client drops records counted before its own.
static BYTE				g_snapResetCount = 0;
// What the last DestroyAllSectorMovers carried: behind the count only while
// fua_snap_debug_resetsplit defers the send.
static BYTE				g_snapResetCountSent = 0;
// Missing-packet requests for a sequence not yet sent, this report window.
static unsigned int		g_unsentRequests = 0;

//*****************************************************************************
//	COSMETIC CLASSES
//
// Each class's verdict, inert or networked with the first failed test, taken
// once per map, lazily (design doc 6.3, Classification).

enum
{
	SNAPCOS_UNKNOWN = 0,
	SNAPCOS_INERT,			// passed the inert test (flags ignored)
	SNAPCOS_NETWORKED,		// failed it; why and what in the two tables
	SNAPCOS_VISITING,		// being classified (a spawn reaching it is a cycle)
};
// Beside the verdict, per class.
enum
{
	SNAPCOSF_CLIENTSIDE		= 1,	// +CLIENTSIDEONLY
	SNAPCOSF_NONETID		= 2,	// +NONETID (or fua_snap_nonetid_classes)
	SNAPCOSF_MISSILE		= 4,	// a missile passed by the relaxed check
	SNAPCOSF_SOLIDONLY		= 8,	// failed only for being solid
	SNAPCOSF_ACSNAMED		= 16,	// an ACS string names it
	SNAPCOSF_MARKER			= 32,	// a marker engine code finds by class
	SNAPCOSF_NEVER			= 64,	// listed in fua_snap_cosmetic_never: never routed
};
// The events note's table. +NONETID only says "no NetID", so it takes a row of
// its own only when the class is not inert.
enum
{
	SNAPCOS_ROW_NONE = 0,
	SNAPCOS_ROW_GONE,			// +CLIENTSIDEONLY, inert: no server copy past the flush
	SNAPCOS_ROW_KEPT,			// +CLIENTSIDEONLY, not inert: server copy kept
	SNAPCOS_ROW_INERT,			// inert, not +CLIENTSIDEONLY
	SNAPCOS_ROW_PLAIN,			// +NONETID, not inert: a plain NetID-0 client copy
	SNAPCOS_ROW_BLOOD,			// a blood spawn whose class is in no row above
	SNAPCOS_ROWS
};
static const char* const g_cosRowNames[SNAPCOS_ROWS] =
	{ "networked", "no server copy", "server copy kept", "inert", "plain copy", "blood" };
static BYTE*			g_cosVerdict = NULL;		// by classId
static BYTE*			g_cosFlags = NULL;			// by classId: SNAPCOSF_*
static const char**		g_cosWhy = NULL;			// by classId: the failed test
static const char**		g_cosWhat = NULL;			// by classId: its subject (function, class)
static bool				g_cosReady = false;
static BYTE				g_cosStamp = 0;
static TArray<actionf_p> g_cosSafe;					// cosmetic action functions
static TMap<const PClass*, bool> g_cosSpecialOwners;	// own a parameter expression that runs a special
static bool				g_cosOwnersBuilt = false;
static bool				g_cosFuncsFound = false;
static BYTE				g_cosPrintedStamp = 0;
static bool				g_cosPrinted = false;
static double			g_cosClassifyMS = 0.0;		// the last full pass
// The override lists by class, 1 forcing it inert past every test but the
// markers and 2 never, a change reclassifying.
static TMap<const PClass*, BYTE> g_cosOverride;
static const PClass*	g_cosMarkerTypes[3];
CUSTOM_CVAR(String, fua_snap_cosmetic_force, "", CVAR_ARCHIVE | CVAR_NOSETBYACS)
{
	g_cosReady = false;
}
CUSTOM_CVAR(String, fua_snap_cosmetic_never, "", CVAR_ARCHIVE | CVAR_NOSETBYACS)
{
	g_cosReady = false;
}
static double snap_RawToMS(QWORD raw);

// A spawning action's class, flags and tid parameters, with the flag bits for a
// client-side spawn and a telefrag (-1: none).
// [Kaori-Chani] File-local, so an engine or module file that one day names the same type
// cannot quietly link against this one.
namespace {
struct SnapCosSpawner
{
	const char*	name;
	int			classParam;
	int			flagsParam;
	int			clientBit;
	int			telefragBit;
	int			tidParam;
	actionf_p	func;
};
}
static SnapCosSpawner g_cosSpawners[] =
{
	{ "A_SpawnItem",		0, -1, 0, 0, -1,				NULL },
	{ "A_SpawnItemEx",		0, 8, 1 << 7, 1 << 6, 10,		NULL },	// SIXF_CLIENTSIDE, SIXF_TELEFRAG
	{ "A_CustomMissile",	0, -1, 0, 0, -1,				NULL },
	{ "A_SpawnDebris",		0, -1, 0, 0, -1,				NULL },
};
static actionf_p g_cosAcsNamed[2] = { NULL, NULL };	// ACS_NamedExecute(Always)
static actionf_p g_cosCustomMissile = NULL;
// By class index: 0 unknown, 1 no spawner state, 2 spawner state, never
// cleared, since the answer depends only on the class's states.
static BYTE* g_classSpawns = NULL;

// Kept short and certain: each changes only how the actor looks, sounds or
// moves itself, or picks its next state.
static const char* const g_cosSafeNames[] =
{
	"A_FadeOut", "A_FadeIn", "A_FadeTo", "A_SetScale", "A_SetTranslucent",
	"A_SetAngle", "A_SetPitch", "A_Stop", "A_ScaleVelocity", "A_ChangeVelocity",
	"A_SetGravity", "A_Gravity", "A_LowGravity", "A_NoGravity",
	"A_PlaySound", "A_PlaySoundEx", "A_StopSound", "A_StopSoundEx",
	"A_Jump", "A_JumpIf", "A_SetTics", "A_Weave", "A_SetUserVar",
	"A_SetFloorClip", "A_UnSetFloorClip", "A_HideThing", "A_UnHideThing",
	"A_SetShadow", "A_ClearShadow",
	"A_CheckFloor", "A_CheckCeiling", "A_CheckRange", "A_CheckSightOrRange",
};

// The spawner and ACS functions; the client needs them without the rest.
static void snap_CosFindFuncs()
{
	if (g_cosFuncsFound)
		return;
	g_cosFuncsFound = true;
	for (unsigned int i = 0; i < countof(g_cosSpawners); ++i)
	{
		AFuncDesc* pDesc = FindFunction(g_cosSpawners[i].name);
		g_cosSpawners[i].func = (pDesc != NULL) ? pDesc->Function : NULL;
		if (strcmp(g_cosSpawners[i].name, "A_CustomMissile") == 0)
			g_cosCustomMissile = g_cosSpawners[i].func;
	}
	AFuncDesc* pNamed = FindFunction("ACS_NamedExecute");
	AFuncDesc* pNamedAlways = FindFunction("ACS_NamedExecuteAlways");
	g_cosAcsNamed[0] = (pNamed != NULL) ? pNamed->Function : NULL;
	g_cosAcsNamed[1] = (pNamedAlways != NULL) ? pNamedAlways->Function : NULL;
}

// A line special or CallACS inside a parameter expression runs whenever
// the expression is evaluated, whatever function it is passed to.
static void snap_CosBuildOwners()
{
	g_cosSpecialOwners.Clear();
	for (unsigned int i = 0; i < StateParams.Size(); ++i)
	{
		const PClass* pOwner = StateParams.SpecialCallOwner(i);
		if (pOwner != NULL)
			g_cosSpecialOwners[pOwner] = true;
	}
	g_cosOwnersBuilt = true;
}

static void snap_CosInit()
{
	if (g_cosVerdict == NULL)
	{
		g_cosVerdict = new BYTE[SNAP_MAXID];
		g_cosFlags = new BYTE[SNAP_MAXID];
		g_cosWhy = new const char*[SNAP_MAXID];
		g_cosWhat = new const char*[SNAP_MAXID];
		for (unsigned int i = 0; i < countof(g_cosSafeNames); ++i)
		{
			AFuncDesc* pDesc = FindFunction(g_cosSafeNames[i]);
			if (pDesc != NULL)
				g_cosSafe.Push(pDesc->Function);
		}
		snap_CosFindFuncs();
	}
	memset(g_cosVerdict, SNAPCOS_UNKNOWN, SNAP_MAXID);
	memset(g_cosFlags, 0, SNAP_MAXID);
	memset(g_cosWhy, 0, SNAP_MAXID * sizeof(const char*));
	memset(g_cosWhat, 0, SNAP_MAXID * sizeof(const char*));
	snap_CosBuildOwners();
}

bool zx::netsnapshot::IsSpawnerState(const FState* pState)
{
	if ((pState == NULL) || (pState->ActionFunc == NULL))
		return false;
	snap_CosFindFuncs();
	for (unsigned int i = 0; i < countof(g_cosSpawners); ++i)
		if (g_cosSpawners[i].func == pState->ActionFunc)
			return true;
	return false;
}

static FxExpression* snap_CosConstParam(const FState* pState, int param);

// The action's own tests, in its order (thingdef_codeptr.cpp A_CustomMissile):
// the client gate on the raw class parameter, then the target unless aim mode 2.
int zx::netsnapshot::CustomMissileNeedsTarget(const AActor* pSelf, const FState* pState)
{
	if ((pSelf == NULL) || (pState == NULL) || (pState->ActionFunc == NULL))
		return 0;
	snap_CosFindFuncs();
	if ((g_cosCustomMissile == NULL) || (pState->ActionFunc != g_cosCustomMissile))
		return 0;
	FxExpression* pClass = snap_CosConstParam(pState, 0);
	FxExpression* pFlags = snap_CosConstParam(pState, 4);
	if ((pClass == NULL) || (pFlags == NULL))
		return 2;
	const PClass* pSpawn = pClass->EvalExpression(NULL).GetClass();
	if (NETWORK_ShouldActorNotBeSpawned(pSelf, pSpawn))
		return 0;
	return ((pFlags->EvalExpression(NULL).GetInt() & 3) == 2) ? 0 : 1;	// CMF_AIMMODE
}

bool zx::netsnapshot::ClassSpawns(const PClass* pType)
{
	if (pType == NULL)
		return false;
	const unsigned int idx = pType->getActorNetworkIndex();
	if (g_classSpawns == NULL)
	{
		g_classSpawns = new BYTE[SNAP_MAXID];
		memset(g_classSpawns, 0, SNAP_MAXID);
	}
	if ((idx != 0) && (g_classSpawns[idx] != 0))
		return g_classSpawns[idx] == 2;
	bool bSpawns = false;
	for (const PClass* p = pType; (p != NULL) && (p != RUNTIME_CLASS(AActor)) && (bSpawns == false); p = p->ParentClass)
	{
		if (p->ActorInfo == NULL)
			continue;
		for (int s = 0; (s < p->ActorInfo->NumOwnedStates) && (bSpawns == false); ++s)
			bSpawns = IsSpawnerState(&p->ActorInfo->OwnedStates[s]);
	}
	if (idx != 0)
		g_classSpawns[idx] = bSpawns ? 2 : 1;
	return bSpawns;
}

WORD zx::netsnapshot::EnterTarget(unsigned int id)
{
	return ((g_targetAtId != NULL) && (id < SNAP_MAXID)) ? g_targetAtId[id] : (WORD)0;
}

bool zx::netsnapshot::OwnsSpecialExpression(const PClass* pType)
{
	if (g_cosOwnersBuilt == false)
		snap_CosBuildOwners();
	return (pType != NULL) && (g_cosSpecialOwners.CheckKey(pType) != NULL);
}

// A state parameter that is a constant, or NULL.
static FxExpression* snap_CosConstParam(const FState* pState, int param)
{
	FxExpression* pExpr = StateParams.Get(pState->ParameterIndex - 1 + param);
	return ((pExpr != NULL) && pExpr->isConstant()) ? pExpr : NULL;
}

static BYTE snap_ClassifyCosmetic(const PClass* pType);

// True if one state's action cannot affect play; otherwise why and what.
// pSelf is the class being classified: spawning itself is not a cycle.
static bool snap_CosActionOk(const PClass* pSelf, const FState* pState, const char*& why, const char*& what)
{
	const actionf_p func = pState->ActionFunc;
	if (func == NULL)
		return true;
	for (unsigned int i = 0; i < g_cosSafe.Size(); ++i)
		if (g_cosSafe[i] == func)
			return true;

	for (unsigned int i = 0; i < countof(g_cosSpawners); ++i)
	{
		const SnapCosSpawner& sp = g_cosSpawners[i];
		if ((sp.func == NULL) || (sp.func != func))
			continue;
		if (sp.flagsParam >= 0)
		{
			FxExpression* pFlags = snap_CosConstParam(pState, sp.flagsParam);
			if (pFlags == NULL)
			{
				why = "spawns with computed flags in";
				what = sp.name;
				return false;
			}
			const int flags = pFlags->EvalExpression(NULL).GetInt();
			if (flags & sp.clientBit)
				return true;		// client-side spawn: the server never makes it
			if (flags & sp.telefragBit)
			{
				why = "telefrags with";
				what = sp.name;
				return false;
			}
		}
		if (sp.tidParam >= 0)
		{
			FxExpression* pTid = snap_CosConstParam(pState, sp.tidParam);
			if ((pTid == NULL) || (pTid->EvalExpression(NULL).GetInt() != 0))
			{
				why = "gives its spawn a tid with";
				what = sp.name;
				return false;
			}
		}
		FxExpression* pClass = snap_CosConstParam(pState, sp.classParam);
		if (pClass == NULL)
		{
			why = "spawns a computed class with";
			what = sp.name;
			return false;
		}
		const PClass* pSpawn = pClass->EvalExpression(NULL).GetClass();
		if ((pSpawn == NULL) || (pSpawn->ActorInfo == NULL))
			return true;			// spawns nothing
		pSpawn = pSpawn->ActorInfo->GetReplacement()->Class;
		if (pSpawn == pSelf)
			return true;			// itself: its other states still decide
		// +CLIENTSIDEONLY is the author's word; +NONETID is not (a server helper).
		if (GetDefaultByType(pSpawn)->NetworkFlags & NETFL_CLIENTSIDEONLY)
			return true;
		const BYTE v = snap_ClassifyCosmetic(pSpawn);
		if (v == SNAPCOS_INERT)
			return true;
		why = (v == SNAPCOS_VISITING) ? "spawns, in a cycle," : "spawns networked";
		what = pSpawn->TypeName.GetChars();
		return false;
	}

	if ((func == g_cosAcsNamed[0]) || (func == g_cosAcsNamed[1]))
	{
		FxExpression* pName = snap_CosConstParam(pState, 0);
		FxExpression* pMap = snap_CosConstParam(pState, 1);
		if ((pMap == NULL) || (pMap->EvalExpression(NULL).GetInt() != 0))
		{
			why = "runs a script on another or computed map with";
			what = "ACS_NamedExecute";
			return false;
		}
		if (pName != NULL)
		{
			const FName name = pName->EvalExpression(NULL).GetName();
			FBehavior* pModule = NULL;
			const ScriptPtr* pScript = FBehavior::StaticFindScript(-int(name), pModule);
			if ((pScript != NULL) && (pScript->Flags & SCRIPTF_ClientSide))
				return true;
			why = "runs server script";
			what = name.GetChars();
			return false;
		}
		why = "runs a computed script with";
		what = "ACS_NamedExecute";
		return false;
	}

	why = "calls";
	what = FindFunctionName(func);
	if (what == NULL)
		what = "an unnamed action function";
	return false;
}

static BYTE snap_CosFail(unsigned int id, const char* why, const char* what)
{
	g_cosVerdict[id] = SNAPCOS_NETWORKED;
	g_cosWhy[id] = why;
	g_cosWhat[id] = what;
	return SNAPCOS_NETWORKED;
}

static BYTE snap_ClassifyCosmetic(const PClass* pType)
{
	if ((pType == NULL) || (pType->ActorInfo == NULL))
		return SNAPCOS_NETWORKED;
	// Index 0 is shared by every class without one (DynamicLight and its
	// children), so it cannot hold a verdict; none of them reach the wire.
	const unsigned int id = pType->getActorNetworkIndex();
	if ((id == 0) || (id >= SNAP_MAXID))
		return SNAPCOS_NETWORKED;
	if (g_cosVerdict[id] != SNAPCOS_UNKNOWN)
		return g_cosVerdict[id];
	g_cosVerdict[id] = SNAPCOS_VISITING;

	const AActor* pDef = GetDefaultByType(pType);
	if (pDef->NetworkFlags & NETFL_SERVERSIDEONLY)
		return snap_CosFail(id, "is", "server-side only");
	// Recorded for the row; the test below ignores them.
	if (pDef->NetworkFlags & NETFL_CLIENTSIDEONLY)
		g_cosFlags[id] |= SNAPCOSF_CLIENTSIDE;
	if (pDef->NetworkFlags & NETFL_NONETID)
		g_cosFlags[id] |= SNAPCOSF_NONETID;

	// Engine code finds markers by class (P_GetPushThing, P_SelectTeleDest),
	// and a client pusher resolves its source by NetID.
	for (unsigned int k = 0; k < countof(g_cosMarkerTypes); ++k)
		if ((g_cosMarkerTypes[k] != NULL) && pType->IsDescendantOf(g_cosMarkerTypes[k]))
		{
			g_cosFlags[id] |= SNAPCOSF_MARKER;
			return snap_CosFail(id, "is", "a marker");
		}
	const BYTE* pOverride = g_cosOverride.CheckKey(pType);
	if ((pOverride != NULL) && (*pOverride == 2))
	{
		g_cosFlags[id] |= SNAPCOSF_NEVER;
		return snap_CosFail(id, "is listed in", "fua_snap_cosmetic_never");
	}
	if (pOverride != NULL)
	{
		g_cosVerdict[id] = SNAPCOS_INERT;
		g_cosWhy[id] = "listed in";
		g_cosWhat[id] = "fua_snap_cosmetic_force";
		return SNAPCOS_INERT;
	}

	// Play flags first, needing no state walk, and solid last.
	if (pDef->flags & MF_SHOOTABLE)					return snap_CosFail(id, "is", "shootable");
	if (pDef->flags & MF_MISSILE)
	{
		// +CLIENTSIDEONLY only: a non-ripper with no damage, poison or
		// +FORCEPAIN returns from PIT_CheckThing before P_DamageMobj.
		if (((pDef->NetworkFlags & NETFL_CLIENTSIDEONLY) == 0) || (pDef->Damage != 0)
			|| (pDef->PoisonDamage > 0) || (pDef->flags2 & MF2_RIP) || (pDef->flags6 & MF6_FORCEPAIN))
			return snap_CosFail(id, "is", "a missile");
		g_cosFlags[id] |= SNAPCOSF_MISSILE;
	}
	if (pDef->flags & MF_SPECIAL)					return snap_CosFail(id, "is", "a pickup");
	if (pDef->flags & MF_PICKUP)					return snap_CosFail(id, "is", "a picker-up");
	if (pDef->flags & (MF_COUNTKILL | MF_COUNTITEM))	return snap_CosFail(id, "is", "counted");
	if (pDef->flags2 & MF2_PUSHABLE)				return snap_CosFail(id, "is", "pushable");
	if (pDef->flags3 & MF3_ISMONSTER)				return snap_CosFail(id, "is", "a monster");
	if (((pDef->flags6 & MF6_NOTRIGGER) == 0)
		&& (pDef->flags2 & (MF2_MCROSS | MF2_PCROSS | MF2_PUSHWALL | MF2_IMPACT)))
		return snap_CosFail(id, "can", "activate lines");
	if (pDef->Damage != 0)							return snap_CosFail(id, "has", "damage");
	if (pType->IsDescendantOf(RUNTIME_CLASS(AInventory)))	return snap_CosFail(id, "is", "inventory");
	if (pType->IsDescendantOf(RUNTIME_CLASS(APlayerPawn)))	return snap_CosFail(id, "is", "a player");
	// Native code (a pusher, a sector action, a spawner's Tick) is invisible to
	// a state walk, so only DECORATE classes built on AActor itself qualify.
	const PClass* pNative = pType;
	while ((pNative != NULL) && pNative->bRuntimeClass)
		pNative = pNative->ParentClass;
	if ((pNative != NULL) && (pNative != RUNTIME_CLASS(AActor)))
		return snap_CosFail(id, "has native code from", pNative->TypeName.GetChars());
	for (const PClass* p = pType; (p != NULL) && (p != RUNTIME_CLASS(AActor)); p = p->ParentClass)
		if (g_cosSpecialOwners.CheckKey(p) != NULL)
			return snap_CosFail(id, "runs a special or script in an expression, in", p->TypeName.GetChars());

	// Every state it can reach, its ancestors' and its labels' included,
	// but not AActor's generic ones.
	FActorInfo* const pBase = RUNTIME_CLASS(AActor)->ActorInfo;
	TArray<const FState*> work;
	for (const PClass* p = pType; (p != NULL) && (p != RUNTIME_CLASS(AActor)); p = p->ParentClass)
	{
		if (p->ActorInfo == NULL)
			continue;
		for (int s = 0; s < p->ActorInfo->NumOwnedStates; ++s)
			work.Push(&p->ActorInfo->OwnedStates[s]);
	}
	TArray<const FStateLabels*> labels;
	if (pType->ActorInfo->StateList != NULL)
		labels.Push(pType->ActorInfo->StateList);
	for (unsigned int l = 0; l < labels.Size(); ++l)
	{
		const FStateLabels* pList = labels[l];
		for (int k = 0; k < pList->NumLabels; ++k)
		{
			if (pList->Labels[k].State != NULL)
				work.Push(pList->Labels[k].State);
			if (pList->Labels[k].Children != NULL)
				labels.Push(pList->Labels[k].Children);
		}
	}

	TMap<const FState*, bool> seen;
	while (work.Size() > 0)
	{
		const FState* pState = NULL;
		work.Pop(pState);
		for (; pState != NULL; pState = pState->NextState)
		{
			if (pBase->OwnsState(pState) || (seen.CheckKey(pState) != NULL))
				break;
			seen[pState] = true;

			const char* why = NULL;
			const char* what = NULL;
			if (snap_CosActionOk(pType, pState, why, what) == false)
				return snap_CosFail(id, why, what);
		}
	}

	// Last, so a class failing on nothing else is counted (layer 1's sizing).
	if (pDef->flags & MF_SOLID)
	{
		g_cosFlags[id] |= SNAPCOSF_SOLIDONLY;
		return snap_CosFail(id, "is", "solid");
	}

	g_cosVerdict[id] = SNAPCOS_INERT;
	g_cosWhy[id] = "inert";
	g_cosWhat[id] = "";
	return SNAPCOS_INERT;
}

// Classes an ACS string names (layer 1): lookups by class name run on the
// server's copies, so a named row-1 class is to keep one.
static unsigned int g_cosAcsClasses = 0, g_cosAcsRow1 = 0;
static BYTE snap_CosRow(unsigned int id);

static void snap_CosMarkAcsNamed(const PClass* pType)
{
	const unsigned int id = pType->getActorNetworkIndex();
	if ((id == 0) || (id >= SNAP_MAXID) || (g_cosFlags[id] & SNAPCOSF_ACSNAMED))
		return;
	if (snap_CosRow(id) == SNAPCOS_ROW_GONE)
		++g_cosAcsRow1;
	g_cosFlags[id] |= SNAPCOSF_ACSNAMED;
	++g_cosAcsClasses;
}

// One override list into g_cosOverride; unknown names are reported once a
// map has loaded (the config sets the cvar before any class resolves).
static void snap_CosParseOverride(const char* pszList, BYTE value, const char* pszName)
{
	FString token;
	for (const char* p = pszList; ; ++p)
	{
		if ((*p != '\0') && (*p != ',') && (*p != ' ') && (*p != '\t'))
		{
			token += *p;
			continue;
		}
		if (token.Len() > 0)
		{
			const PClass* pType = PClass::FindClass(token.GetChars());
			if ((pType == NULL) || (pType->IsDescendantOf(RUNTIME_CLASS(AActor)) == false))
			{
				if (gamestate == GS_LEVEL)
					Printf("%s: unknown actor class '%s'\n", pszName, token.GetChars());
			}
			else
				g_cosOverride[pType] = value;
			token = "";
		}
		if (*p == '\0')
			break;
	}
}

// Every loaded module's string table; runtime strings (StrParam) are not seen.
static void snap_CosScanAcs()
{
	g_cosAcsClasses = g_cosAcsRow1 = 0;
	for (int lib = 0; ; ++lib)
	{
		const FBehavior* pModule = FBehavior::StaticGetModule(lib);
		if (pModule == NULL)
			break;
		for (DWORD i = 0; ; ++i)
		{
			const char* psz = pModule->LookupString(i);
			if (psz == NULL)
				break;
			const PClass* pType = PClass::FindClass(psz);
			if ((pType == NULL) || (pType->ActorInfo == NULL) || !pType->IsDescendantOf(RUNTIME_CLASS(AActor)))
				continue;
			snap_CosMarkAcsNamed(pType);
			snap_CosMarkAcsNamed(pType->ActorInfo->GetReplacement()->Class);
		}
	}
}

// Classify every actor class, once per map.
static void snap_CosEnsure()
{
	if (g_cosReady && (g_cosStamp == g_snapMapStamp))
		return;
	// Timed: the first pass can run inside an action function mid-tic.
	const tictime_raw_t start = TIC_GetRawCounter();
	snap_CosInit();
	g_cosMarkerTypes[0] = PClass::FindClass(NAME_PointPusher);
	g_cosMarkerTypes[1] = PClass::FindClass(NAME_PointPuller);
	g_cosMarkerTypes[2] = PClass::FindClass(NAME_TeleportDest);
	g_cosOverride.Clear();
	snap_CosParseOverride(fua_snap_cosmetic_force, 1, "fua_snap_cosmetic_force");
	snap_CosParseOverride(fua_snap_cosmetic_never, 2, "fua_snap_cosmetic_never");
	for (unsigned int i = 0; i < PClass::m_Types.Size(); ++i)
	{
		const PClass* pType = PClass::m_Types[i];
		if ((pType != NULL) && (pType->ActorInfo != NULL) && pType->IsDescendantOf(RUNTIME_CLASS(AActor)))
			snap_ClassifyCosmetic(pType);
	}
	snap_CosScanAcs();
	g_cosClassifyMS = snap_RawToMS((QWORD)(TIC_GetRawCounter() - start));
	g_cosReady = true;
	g_cosStamp = g_snapMapStamp;
}

// The class's row in the events note's table; the blood row is decided by the
// spawn site (NoteBloodSpawn), not here.
static BYTE snap_CosRow(unsigned int id)
{
	if ((id == 0) || (id >= SNAP_MAXID) || (g_cosVerdict == NULL))
		return SNAPCOS_ROW_NONE;
	// Whatever its flags: a +NONETID class goes stock's way, not row 4.
	if (g_cosFlags[id] & SNAPCOSF_NEVER)
		return SNAPCOS_ROW_NONE;
	const bool bInert = (g_cosVerdict[id] == SNAPCOS_INERT);
	// An ACS-named class keeps its server copy: lookups by class name run on it.
	if (g_cosFlags[id] & SNAPCOSF_CLIENTSIDE)
		return (bInert && ((g_cosFlags[id] & SNAPCOSF_ACSNAMED) == 0)) ? SNAPCOS_ROW_GONE : SNAPCOS_ROW_KEPT;
	if (bInert)
		return SNAPCOS_ROW_INERT;
	return (g_cosFlags[id] & SNAPCOSF_NONETID) ? SNAPCOS_ROW_PLAIN : SNAPCOS_ROW_NONE;
}

static bool snap_CosIsCosmetic(unsigned int id)
{
	return snap_CosRow(id) != SNAPCOS_ROW_NONE;
}

// The client copy gets NETFL_CLIENTSIDEONLY, so it makes its own sounds and
// children, and the server copy carries the suppression flag.
static bool snap_CosRowFlagsCopy(BYTE row)
{
	return (row == SNAPCOS_ROW_GONE) || (row == SNAPCOS_ROW_KEPT) || (row == SNAPCOS_ROW_INERT);
}

// NetIDs taken by what a cosmetic actor's action spawned, per child class,
// reset with the report window.
AActor* g_pSnapActionSelf = NULL;
static unsigned int* g_cosInheritByClass = NULL;	// networked children, by class
static unsigned int g_cosInheritTotal = 0;			// networked children
static unsigned int g_cosInheritOverlap = 0;		// cosmetic children: already layer 1

void zx::netsnapshot::NoteSpawnNetID(AActor* pActor)
{
	if (NETWORK_GetState() != NETSTATE_SERVER)
		return;
	AActor* const pSpawner = g_pSnapActionSelf;
	if ((pSpawner == NULL) || (pActor == NULL) || (pSpawner == pActor))
		return;
	snap_CosEnsure();
	if (snap_CosIsCosmetic(pSpawner->GetClass()->getActorNetworkIndex()) == false)
		return;
	const unsigned int id = pActor->GetClass()->getActorNetworkIndex();
	if ((id == 0) || (id >= SNAP_MAXID))
		return;
	// Only a networked child counts, a cosmetic one being classified.
	if (snap_CosIsCosmetic(id))
	{
		++g_cosInheritOverlap;
		return;
	}
	if (g_cosInheritByClass == NULL)
	{
		g_cosInheritByClass = new unsigned int[SNAP_MAXID];
		memset(g_cosInheritByClass, 0, SNAP_MAXID * sizeof(unsigned int));
	}
	++g_cosInheritByClass[id];
	++g_cosInheritTotal;
}

//*****************************************************************************
//	COSMETIC EVENTS
//
// Mode 1 counts what the event route would send; mode 2 sends it, and each
// client makes its own copy.

// Per routed actor from Spawn to Destroy, which alone drops it, or a reused
// address would inherit the entry.
enum
{
	SNAPCOSM_TICK		= 1,	// spawned from native Tick code, not an action
	SNAPCOSM_BYCOPY		= 2,	// the spawner's client copy makes it (layer 2)
	SNAPCOSM_BLOOD		= 4,	// a blood site: its event goes at the call
	SNAPCOSM_RIPPER		= 8,	// ripper blood: a generic event whatever its spawner
	SNAPCOSM_UNTOLD		= 16,	// P_SpawnPuff told no client: counted, still an event
	SNAPCOSM_FLUSHED	= 32,
	SNAPCOSM_HELD		= 64,	// flushed, its event held for a server script
	SNAPCOSM_SCRIPT		= 128,	// a server script has it as activator
};
namespace {
struct SnapCosMark
{
	BYTE		row;
	BYTE		flags;
	signed char	shooter;	// the player whose own puff this is, predicted; -1
	BYTE		tidRule;	// the TID rule's reason at the flush (layer 1), 0 none
	bool		bAcs;		// spawned by DLevelScript::DoSpawn
	int			heldTic;	// the tic its hold began
	int			spawnTic;
	DThinker*	spawner;	// read only at its children's flush, in the same pass
};
}
static TMap<DThinker*, SnapCosMark> g_cosMarks;
// Copies the TID rule refused at their flush this pump, for their children's.
static TMap<DThinker*, bool> g_ceRefused;
static TArray<AActor*> g_ceHeld;	// for the deadline
// The row by NetID, for the snapshot's records; set at every spawn, so a
// reused id describes its newest owner.
static BYTE*		g_cosNetRow = NULL;

// The command's header (SVC2 2, map stamp 1, count 2), the largest generic
// event being SNAP_COSE_GENERIC_MAX.
#define SNAP_COSEVENT_HEADER	5
#define SNAP_COSEVENT_MAXBYTES	( SNAP_COSE_GENERIC_MAX + 1 )
namespace {
struct SnapCosEvent
{
	fixed_t		x, y, z;
	int			tic;			// gametic it was made, for its age within the pump
	WORD		originator;		// blood: the NetID the client must resolve
	BYTE		kind;
	BYTE		len;
	signed char	shooter;
	BYTE		data[SNAP_COSEVENT_MAXBYTES];	// the flags byte first; the age goes in at the pump
};
}
static TArray<SnapCosEvent> g_ceEvents;	// this pump's
static unsigned int	g_cePumpTics = 1;	// tics the pump simulated
// Per client: bytes saved while quiet, and the last pump's overflow, carried
// one pump.
static unsigned int	g_ceBank[MAXPLAYERS];
static TArray<SnapCosEvent> g_ceCarry[MAXPLAYERS];
static TArray<SnapCosEvent> g_ceCarryNext;

// Report counters, per tic on the spawn side and per client per tic otherwise.
static QWORD		g_ceGeneric[SNAPCOS_ROWS];	// generic events by row
static QWORD		g_ceBlood = 0, g_ceSplatter = 0;
static QWORD		g_ceByCopy = 0, g_ceDiedEarly = 0, g_ceUntold = 0, g_ceFromTick = 0;
static QWORD		g_ceVelBig = 0, g_ceVelSmall = 0, g_ceStateByte = 0, g_ceStateOutside = 0;
static QWORD		g_ceExtras = 0, g_ceRelaxed = 0, g_ceRemoved = 0;
static unsigned int* g_ceNotInertByClass = NULL;	// rows 2 and 4, by class
static QWORD		g_ceClientTics = 0, g_ceKept = 0, g_ceCulled = 0, g_ceShooterSkips = 0;
static QWORD		g_ceWanted = 0, g_ceSent = 0, g_ceCapDropped = 0, g_cePumpsCapped = 0;
static QWORD		g_ceClientPumps = 0, g_ceAppended = 0, g_ceAlone = 0;
static QWORD		g_ceAgeSaturated = 0, g_ceBloodKept = 0, g_ceBloodUnknown = 0;
static QWORD		g_ceTodayRecs[SNAPCOS_ROWS], g_ceTodayBytes[SNAPCOS_ROWS];
static QWORD		g_ceCarried = 0, g_ceCarryAged = 0, g_ceCarryOver = 0, g_ceBankAtCap = 0;
static QWORD		g_ceHeldN = 0, g_ceRelTick = 0, g_ceRelScript = 0, g_ceRelDeadline = 0;
static QWORD		g_ceRemovedHeld = 0, g_ceScriptAfter = 0;
// Layer 1, per window: the TID rule (tid, special, args), children left with no
// producer, row-1 copies of ACS-named classes, routed copies alive at a join.
static QWORD		g_ceTidRule[4], g_ceOrphaned = 0, g_ceAcsKept = 0;
static QWORD		g_ceAcsRouted = 0, g_ceAcsClientSide = 0;	// routed ACS spawns
static QWORD		g_ceJoins = 0, g_ceJoinAlive = 0, g_ceJoinWorst = 0;
static unsigned int* g_ceAgeByClass = NULL;	// scratch for the report
// Layer 1, per map: networked map things clients could make, why the rest
// could not, and what the listed ones cost the snapshot now.
static unsigned int	g_cmNetworked = 0, g_cmListed = 0, g_cmLive = 0, g_cmSolidOnly = 0;
static unsigned int	g_cmRefused[4];		// marker, tid, special, args
static unsigned int* g_cmListedByClass = NULL;
static unsigned int* g_cmSolidByClass = NULL;
static BYTE*		g_cmByNetID = NULL;		// 1: a listed map thing, 2: solid, failing nothing else
static QWORD		g_cmTodayRecs = 0, g_cmTodayBytes = 0;
static QWORD		g_cmSolidRecs = 0, g_cmSolidBytes = 0;

static void snap_CosCountersReset()
{
	for (unsigned int r = 0; r < SNAPCOS_ROWS; ++r)
		g_ceGeneric[r] = g_ceTodayRecs[r] = g_ceTodayBytes[r] = 0;
	g_ceBlood = g_ceSplatter = 0;
	g_ceByCopy = g_ceDiedEarly = g_ceUntold = g_ceFromTick = 0;
	g_ceVelBig = g_ceVelSmall = g_ceStateByte = g_ceStateOutside = 0;
	g_ceExtras = g_ceRelaxed = g_ceRemoved = 0;
	if (g_ceNotInertByClass != NULL)
		memset(g_ceNotInertByClass, 0, SNAP_MAXID * sizeof(unsigned int));
	g_ceClientTics = g_ceKept = g_ceCulled = g_ceShooterSkips = 0;
	g_ceWanted = g_ceSent = g_ceCapDropped = g_cePumpsCapped = 0;
	g_ceClientPumps = g_ceAppended = g_ceAlone = 0;
	g_ceAgeSaturated = g_ceBloodKept = g_ceBloodUnknown = 0;
	g_ceCarried = g_ceCarryAged = g_ceCarryOver = g_ceBankAtCap = 0;
	g_ceHeldN = g_ceRelTick = g_ceRelScript = g_ceRelDeadline = 0;
	g_ceRemovedHeld = g_ceScriptAfter = 0;
	for (unsigned int k = 0; k < 4; ++k)
		g_ceTidRule[k] = 0;
	g_ceOrphaned = g_ceAcsKept = 0;
	g_ceAcsRouted = g_ceAcsClientSide = 0;
	g_ceJoins = g_ceJoinAlive = g_ceJoinWorst = 0;
	g_cmTodayRecs = g_cmTodayBytes = 0;
	g_cmSolidRecs = g_cmSolidBytes = 0;
}

static void snap_CosMapReset()
{
	g_cmNetworked = g_cmListed = g_cmLive = g_cmSolidOnly = 0;
	for (unsigned int k = 0; k < 4; ++k)
		g_cmRefused[k] = 0;
	if (g_cmByNetID != NULL)
	{
		memset(g_cmByNetID, 0, SNAP_MAXID);
		memset(g_cmListedByClass, 0, SNAP_MAXID * sizeof(unsigned int));
		memset(g_cmSolidByClass, 0, SNAP_MAXID * sizeof(unsigned int));
	}
}

// A client's bank and carried events: a fresh client, or a map whose events
// these are not.
static void snap_CosClientReset(unsigned int client)
{
	if (client >= MAXPLAYERS)
		return;
	g_ceBank[client] = 0;
	g_ceCarry[client].Clear();
}

// Off, on, or a map change: nothing describes live actors any more.
static void snap_CosEventsReset()
{
	g_cosMarks.Clear();
	g_ceRefused.Clear();
	g_ceHeld.Clear();
	g_ceEvents.Clear();
	for (unsigned int c = 0; c < MAXPLAYERS; ++c)
		snap_CosClientReset(c);
	if (g_cosNetRow != NULL)
		memset(g_cosNetRow, SNAPCOS_ROW_NONE, SNAP_MAXID);
	snap_CosCountersReset();
	snap_CosMapReset();
}

// 0 off, 1 report only, 2 act.
CUSTOM_CVAR(Int, fua_snap_cosmetic_events, 0, CVAR_NOSETBYACS)
{
	if ((self < 0) || (self > 2))
	{
		self = clamp<int>(self, 0, 2);
		return;
	}
	snap_CosEventsReset();
}
// Cull distance in map units, its own knob: smoke farther than this from a
// tracer still in view is not shown, a bandwidth trade.
CVAR(Int, fua_snap_cosmetic_dist, 4096, CVAR_NOSETBYACS)
CVAR(Int, fua_snap_cosmetic_bytes, 600, CVAR_NOSETBYACS)	// budget per client per tic simulated

static bool snap_CosActive()
{
	return (fua_snap_cosmetic_events > 0) && snap_GateOn()
		&& (NETWORK_GetState() == NETSTATE_SERVER);
}

// Acting needs the events to reach someone.
static bool snap_CosActs()
{
	return (fua_snap_cosmetic_events >= 2) && fua_snap_send && snap_CosActive();
}

static void snap_CosAlloc()
{
	if (g_cosNetRow != NULL)
		return;
	g_cosNetRow = new BYTE[SNAP_MAXID];
	memset(g_cosNetRow, SNAPCOS_ROW_NONE, SNAP_MAXID);
	g_ceNotInertByClass = new unsigned int[SNAP_MAXID];
	memset(g_ceNotInertByClass, 0, SNAP_MAXID * sizeof(unsigned int));
	g_ceAgeByClass = new unsigned int[SNAP_MAXID];
	g_cmByNetID = new BYTE[SNAP_MAXID];
	g_cmListedByClass = new unsigned int[SNAP_MAXID];
	g_cmSolidByClass = new unsigned int[SNAP_MAXID];
	snap_CosMapReset();
}

// Spawn's spawner: the actor in an action, else the one ticking, else none.
DThinker* g_pSnapTickSelf = NULL;

static bool g_bSnapAcsSpawn = false;	// inside DoSpawn's Spawn

static AActor* snap_CosSpawner()
{
	// No client copy runs server ACS, whatever actor's action started it.
	if (g_bSnapAcsSpawn)
		return NULL;
	if (g_pSnapActionSelf != NULL)
		return g_pSnapActionSelf;
	if ((g_pSnapTickSelf != NULL) && g_pSnapTickSelf->IsKindOf(RUNTIME_CLASS(AActor)))
		return static_cast<AActor*>(g_pSnapTickSelf);
	return NULL;
}

static void snap_CosInsert(AActor* pActor, BYTE row, BYTE flags)
{
	SnapCosMark mark;
	mark.row = row;
	mark.flags = flags;
	mark.shooter = -1;
	mark.tidRule = 0;
	mark.bAcs = false;
	mark.heldTic = 0;
	mark.spawnTic = gametic;
	mark.spawner = snap_CosSpawner();
	g_cosMarks.Insert(pActor, mark);
	const unsigned int id = pActor->GetClass()->getActorNetworkIndex();
	if ((g_cosFlags[id] & (SNAPCOSF_CLIENTSIDE | SNAPCOSF_ACSNAMED)) == (SNAPCOSF_CLIENTSIDE | SNAPCOSF_ACSNAMED)
		&& (g_cosVerdict[id] == SNAPCOS_INERT))
		++g_ceAcsKept;
	if ((row == SNAPCOS_ROW_KEPT) || (row == SNAPCOS_ROW_PLAIN))
		++g_ceNotInertByClass[id];
	if (g_cosFlags[id] & SNAPCOSF_MISSILE)
		++g_ceRelaxed;
	if (flags & SNAPCOSM_TICK)
		++g_ceFromTick;
}

// A generic event whose copy is not inert waits for a server script: rows 2
// and 4, and ripper blood.
static bool snap_CosHeldRow(BYTE row)
{
	return (row == SNAPCOS_ROW_KEPT) || (row == SNAPCOS_ROW_PLAIN) || (row == SNAPCOS_ROW_BLOOD);
}

// Whether the client copy runs this copy's actions, their children and sounds
// its own: a flagged copy, not held and not about to be.
static bool snap_CosCopyActs(const SnapCosMark* pMark)
{
	if ((pMark == NULL) || (snap_CosRowFlagsCopy(pMark->row) == false))
		return false;
	if (pMark->flags & SNAPCOSM_HELD)
		return false;
	const bool bGeneric = ((pMark->flags & SNAPCOSM_BLOOD) == 0)
		&& ((pMark->flags & (SNAPCOSM_BYCOPY | SNAPCOSM_RIPPER)) != SNAPCOSM_BYCOPY);
	return (pMark->flags & SNAPCOSM_FLUSHED) || (bGeneric == false) || (snap_CosHeldRow(pMark->row) == false);
}

bool zx::netsnapshot::NoteCosmeticSpawn(AActor* pActor)
{
	if (pActor == NULL)
		return false;
	if (NETWORK_InClientMode())
	{
		CosmeticSpawn(pActor);
		return false;
	}
	if (snap_CosActive() == false)
		return false;
	snap_CosAlloc();
	// A spawn with no spawning actor stays networked, except an ACS one,
	// tested first so a map's own spawns cost no classification.
	AActor* const pSpawner = snap_CosSpawner();
	if (((pSpawner == NULL) && (g_bSnapAcsSpawn == false)) || (pSpawner == pActor))
		return false;
	snap_CosEnsure();
	const BYTE row = snap_CosRow(pActor->GetClass()->getActorNetworkIndex());
	if (row == SNAPCOS_ROW_NONE)
		return false;
	BYTE flags = ((g_pSnapActionSelf == NULL) && (pSpawner != NULL)) ? SNAPCOSM_TICK : 0;
	if ((pSpawner != NULL) && snap_CosCopyActs(g_cosMarks.CheckKey(pSpawner)))
		flags |= SNAPCOSM_BYCOPY;
	snap_CosInsert(pActor, row, flags);
	return snap_CosActs();
}

void zx::netsnapshot::AcsSpawnScope(bool bOn)
{
	g_bSnapAcsSpawn = bOn && (NETWORK_GetState() == NETSTATE_SERVER);
}

void zx::netsnapshot::NoteAcsSpawn(AActor* pActor)
{
	if ((pActor == NULL) || (g_cosMarks.CountUsed() == 0))
		return;
	SnapCosMark* pMark = g_cosMarks.CheckKey(pActor);
	if (pMark == NULL)
		return;
	pMark->bAcs = true;
	++g_ceAcsRouted;
	if (g_cosFlags[pActor->GetClass()->getActorNetworkIndex()] & SNAPCOSF_CLIENTSIDE)
		++g_ceAcsClientSide;
}

void zx::netsnapshot::NoteCosmeticNetID(AActor* pActor)
{
	if (NETWORK_GetState() != NETSTATE_SERVER)
		return;
	if ((g_cosNetRow == NULL) || (pActor == NULL) || (pActor->NetID == 0))
		return;
	const SnapCosMark* pMark = (g_cosMarks.CountUsed() > 0) ? g_cosMarks.CheckKey(pActor) : NULL;
	g_cosNetRow[pActor->NetID] = (pMark != NULL) ? pMark->row : (BYTE)SNAPCOS_ROW_NONE;
}

bool zx::netsnapshot::CosmeticRouted(const AActor* pActor)
{
	if ((pActor == NULL) || (g_cosMarks.CountUsed() == 0) || (snap_CosActs() == false))
		return false;
	return g_cosMarks.CheckKey(const_cast<AActor*>(pActor)) != NULL;
}

bool zx::netsnapshot::CosmeticMutesSound(const AActor* pActor)
{
	if ((pActor == NULL) || (pActor != g_pSnapActionSelf) || (CosmeticRouted(pActor) == false))
		return false;
	return snap_CosCopyActs(g_cosMarks.CheckKey(const_cast<AActor*>(pActor)));
}

// Every blood spawn takes the route, whatever its verdict or spawner: stock
// never sent the server's blood.
static void snap_CosMarkBlood(AActor* pBlood, BYTE flag)
{
	if ((pBlood == NULL) || (snap_CosActive() == false))
		return;
	snap_CosEnsure();
	snap_CosAlloc();
	const unsigned int id = pBlood->GetClass()->getActorNetworkIndex();
	if ((id == 0) || (id >= SNAP_MAXID))
		return;
	SnapCosMark* pMark = g_cosMarks.CheckKey(pBlood);
	if (pMark != NULL)
		pMark->flags |= flag;
	else
	{
		const BYTE row = snap_CosRow(id);
		snap_CosInsert(pBlood, (row != SNAPCOS_ROW_NONE) ? row : (BYTE)SNAPCOS_ROW_BLOOD, flag);
		pMark = g_cosMarks.CheckKey(pBlood);
	}
	if (pBlood->NetID == 0)
		return;
	if (snap_CosActs())
		pBlood->FreeNetID();
	else
		g_cosNetRow[pBlood->NetID] = pMark->row;
}

void zx::netsnapshot::NoteBloodSpawn(AActor* pBlood)
{
	snap_CosMarkBlood(pBlood, SNAPCOSM_BLOOD);
}

void zx::netsnapshot::NoteRipperBlood(AActor* pBlood)
{
	snap_CosMarkBlood(pBlood, SNAPCOSM_RIPPER);
}

// At the call, not the flush: direction, damage and originator are the
// function's arguments, and stock tells clients even when no actor spawned.
bool zx::netsnapshot::NoteBloodEvent(fixed_t x, fixed_t y, fixed_t z, unsigned int dir,
	int damage, AActor* pOriginator, int which)
{
	if (snap_CosActive() == false)
		return false;
	snap_CosEnsure();
	// The copy's flag follows its class's row; a class in none is the blood row.
	const PClass* pType = (pOriginator != NULL) ? pOriginator->GetBloodType(which) : NULL;
	const bool bPlain = (pType == NULL)
		|| (snap_CosRowFlagsCopy(snap_CosRow(pType->getActorNetworkIndex())) == false);

	SnapCosEvent ev;
	ev.x = x;
	ev.y = y;
	ev.z = z;
	ev.tic = gametic;
	ev.originator = (pOriginator != NULL) ? (WORD)pOriginator->NetID : 0;
	ev.kind = (which == 0) ? SNAP_COSE_BLOOD : SNAP_COSE_SPLATTER;
	ev.shooter = -1;
	SnapCosBlood b;
	b.x = x;
	b.y = y;
	b.z = z;
	b.dir = dir;
	b.damage = damage;
	b.originator = ev.originator;
	ev.len = (BYTE)CosWriteBlood(ev.data, ServerMapFrame(),
		(BYTE)(ev.kind | (bPlain ? SNAP_COSE_BLOODPLAIN : 0) | ((which == 2) ? SNAP_COSE_SPLATTER2 : 0)), b);
	g_ceEvents.Push(ev);
	if (which == 0)
		++g_ceBlood;
	else
		++g_ceSplatter;
	return snap_CosActs();
}

// After P_SpawnPuffForClients, which may have given the puff a NetID.
void zx::netsnapshot::NotePuff(AActor* pPuff, AActor* pSource, bool bTellClients)
{
	if (NETWORK_GetState() != NETSTATE_SERVER)
		return;
	if ((pPuff == NULL) || (g_cosMarks.CountUsed() == 0))
		return;
	SnapCosMark* pMark = g_cosMarks.CheckKey(pPuff);
	if (pMark == NULL)
		return;
	if (pPuff->NetID != 0)
		g_cosNetRow[pPuff->NetID] = pMark->row;
	// Not a puff stock hid: temporary ones die before the flush, rail puffs are
	// sent later by P_SpawnPuffForClients, and stock clients made A_Tracer's.
	if (bTellClients == false)
		pMark->flags |= SNAPCOSM_UNTOLD;
	// The shooter's own client predicts it: P_SpawnPuffForClients' rule, which
	// the rail paths apply when they send.
	if ((pSource != NULL) && (pSource->player != NULL) && (pPuff->NetworkFlags & NETFL_NONETID)
		&& (pSource->player->userinfo.GetClientFlags() & CLIENTFLAGS_CLIENTSIDEPUFFS))
		pMark->shooter = (signed char)(pSource->player - players);
}

void zx::netsnapshot::ForgetCosmetic(AActor* pActor)
{
	if (NETWORK_InClientMode())
	{
		CosmeticForget(pActor);
		return;
	}
	if ((g_cmByNetID != NULL) && (pActor->NetID != 0) && g_cmByNetID[pActor->NetID])
	{
		if (g_cmByNetID[pActor->NetID] == 1)
			--g_cmLive;
		g_cmByNetID[pActor->NetID] = 0;
	}
	if (g_cosMarks.CountUsed() == 0)
		return;
	const SnapCosMark* pMark = g_cosMarks.CheckKey(pActor);
	if (pMark == NULL)
		return;
	if ((pMark->flags & SNAPCOSM_FLUSHED) == 0)
		++g_ceDiedEarly;
	if (pMark->flags & SNAPCOSM_HELD)
	{
		++g_ceRemovedHeld;
		for (unsigned int i = 0; i < g_ceHeld.Size(); ++i)
			if (g_ceHeld[i] == pActor)
			{
				g_ceHeld.Delete(i);
				break;
			}
	}
	g_cosMarks.Remove(pActor);
}

// The state as stock's SetThingFrame names it: owner class and offset, the
// owner by its depth in the actor's ancestor chain. -1 outside 4 + 12 bits.
static int snap_CosStateCode(const AActor* pActor)
{
	const PClass* const pOwner = FState::StaticFindStateOwner(pActor->state);
	if ((pOwner == NULL) || (pOwner->ActorInfo == NULL))
		return -1;
	unsigned int depth = 0;
	const PClass* p = pActor->GetClass();
	while ((p != NULL) && (p != pOwner) && (depth < 15))
	{
		p = p->ParentClass;
		++depth;
	}
	const ptrdiff_t offset = pActor->state - pOwner->ActorInfo->OwnedStates;
	if ((p != pOwner) || (offset < 0) || (offset > 0xFFF))
		return -1;
	return (int)((depth << 12) | (unsigned int)offset);
}

// The generic event, read off the copy at its flush or release.
static void snap_CosBuildGeneric(const AActor* pActor, BYTE row, bool bActed, SnapCosEvent& ev)
{
	SnapCosGeneric g;
	memset(&g, 0, sizeof(g));
	g.classId = (WORD)pActor->GetClass()->getActorNetworkIndex();
	g.x = pActor->x;
	g.y = pActor->y;
	g.z = pActor->z;
	g.angle = pActor->angle;
	g.vel[0] = pActor->velx;
	g.vel[1] = pActor->vely;
	g.vel[2] = pActor->velz;

	g.stateCode = -1;
	if ((pActor->state != NULL) && (pActor->state != pActor->SpawnState))
	{
		++g_ceStateByte;
		const int code = snap_CosStateCode(pActor);
		if (code >= 0)
			g.stateCode = code;
		else
			++g_ceStateOutside;
	}

	const AActor* pDef = GetDefaultByType(pActor->GetClass());
	g.extras = bActed ? SNAP_COSX_ACTED : 0;
	if (pActor->Translation != pDef->Translation)
	{
		g.extras |= SNAP_COSX_TRANSLATION;
		g.transType = (BYTE)(pActor->Translation >> TRANSLATION_SHIFT);
		g.transIndex = (WORD)(pActor->Translation & TRANSLATION_MASK);
	}
	if (pActor->alpha != pDef->alpha)
	{
		g.extras |= SNAP_COSX_ALPHA;
		g.alpha = pActor->alpha;
	}
	if ((pActor->scaleX != pDef->scaleX) || (pActor->scaleY != pDef->scaleY))
	{
		g.extras |= SNAP_COSX_SCALE;
		g.scaleX = pActor->scaleX;
		g.scaleY = pActor->scaleY;
	}
	if (pActor->RenderStyle.AsDWORD != pDef->RenderStyle.AsDWORD)
	{
		g.extras |= SNAP_COSX_STYLE;
		g.style = pActor->RenderStyle.AsDWORD;
	}
	if (g.extras != 0)
		++g_ceExtras;

	bool bBig, bSmall;
	ev.len = (BYTE)CosWriteGeneric(ev.data, ServerMapFrame(),
		snap_CosRowFlagsCopy(row) ? SNAP_COSE_COPY : SNAP_COSE_PLAIN, g, bBig, bSmall);
	g_ceVelBig += bBig ? 1 : 0;
	g_ceVelSmall += bSmall ? 1 : 0;
}

static void snap_CosPushGeneric(const AActor* pActor, const SnapCosMark& mark, int tic, bool bActed)
{
	SnapCosEvent ev;
	ev.x = pActor->x;
	ev.y = pActor->y;
	ev.z = pActor->z;
	ev.tic = tic;
	ev.originator = 0;
	ev.kind = SNAP_COSE_COPY;
	ev.shooter = mark.shooter;
	snap_CosBuildGeneric(pActor, mark.row, bActed, ev);
	g_ceEvents.Push(ev);
	++g_ceGeneric[mark.row];
}

// Releases a held copy as it is now, at the next tic (it is past this one's),
// which starts its suppression and its children's BYCOPY.
static void snap_CosRelease(AActor* pActor, SnapCosMark* pMark, int tic)
{
	pMark->flags &= ~(SNAPCOSM_HELD | SNAPCOSM_SCRIPT);
	for (unsigned int i = 0; i < g_ceHeld.Size(); ++i)
		if (g_ceHeld[i] == pActor)
		{
			g_ceHeld.Delete(i);
			break;
		}
	snap_CosPushGeneric(pActor, *pMark, tic, true);
}

// The 8-tic deadline, before the pump: past the pump's last simulated tic.
static void snap_CosReleaseDue()
{
	for (unsigned int i = g_ceHeld.Size(); i-- > 0; )
	{
		AActor* const pActor = g_ceHeld[i];
		SnapCosMark* pMark = g_cosMarks.CheckKey(pActor);
		if (pMark == NULL)
		{
			g_ceHeld.Delete(i);
			continue;
		}
		if ((gametic - 1) - pMark->heldTic >= 8)
		{
			++g_ceRelDeadline;
			snap_CosRelease(pActor, pMark, gametic);
		}
	}
}

static bool snap_CosHasArgs(const AActor* pActor)
{
	for (unsigned int k = 0; k < 5; ++k)
		if (pActor->args[k] != 0)
			return true;
	return false;
}

// The flush: the spawner has set velocity, angle and state.
void zx::netsnapshot::NoteFirstTick(DThinker* pThinker)
{
	if (NETWORK_InClientMode())
	{
		CosmeticFirstTick(pThinker);
		return;
	}
	if (g_cosMarks.CountUsed() == 0)
		return;
	SnapCosMark* pMark = g_cosMarks.CheckKey(pThinker);
	if ((pMark == NULL) || (pMark->flags & SNAPCOSM_FLUSHED))
		return;
	pMark->flags |= SNAPCOSM_FLUSHED;
	if (pThinker->ObjectFlags & OF_EuthanizeMe)
	{
		++g_ceDiedEarly;
		return;
	}
	AActor* const pActor = static_cast<AActor*>(pThinker);	// only actors are marked
	const BYTE row = pMark->row;
	if (pMark->flags & SNAPCOSM_UNTOLD)
		++g_ceUntold;
	// Stock keeps no server copy of an ACS-spawned +CLIENTSIDEONLY actor
	// (DoSpawn destroys it), so this route keeps none past its event either.
	const bool bAcsClientSide = pMark->bAcs
		&& (g_cosFlags[pActor->GetClass()->getActorNetworkIndex()] & SNAPCOSF_CLIENTSIDE);

	// The TID rule, before the hold and the row-1 destroy: an addressable
	// instance takes a NetID and enters by snapshot.
	if ((pMark->flags & SNAPCOSM_BLOOD) == 0)
	{
		if ((pMark->flags & SNAPCOSM_BYCOPY) && (pMark->spawner != NULL) && (g_ceRefused.CheckKey(pMark->spawner) != NULL))
		{
			++g_ceOrphaned;
			if (snap_CosActs())
				pMark->flags &= ~SNAPCOSM_BYCOPY;
		}
		// Not a child its spawner's client copy makes (un-routed, it would
		// double), nor a +NONETID copy (row 4): stock never gave it a NetID.
		pMark->tidRule = (bAcsClientSide || (pMark->flags & SNAPCOSM_BYCOPY) || (row == SNAPCOS_ROW_PLAIN)) ? 0
			: (pActor->tid != 0) ? 1 : (pActor->special != 0) ? 2 : snap_CosHasArgs(pActor) ? 3 : 0;
		if (pMark->tidRule != 0)
		{
			++g_ceTidRule[pMark->tidRule];
			g_ceRefused[pThinker] = true;
			if (snap_CosActs())
			{
				g_cosMarks.Remove(pThinker);
				pActor->NetID = g_ActorNetIDList.getNewID();
				g_ActorNetIDList.useID(pActor->NetID, pActor);
				if (g_cosNetRow != NULL)
					g_cosNetRow[pActor->NetID] = SNAPCOS_ROW_NONE;
				return;
			}
		}
	}
	const BYTE mflags = pMark->flags;

	if (mflags & SNAPCOSM_BLOOD)
		;	// its event went at the call
	else if ((mflags & (SNAPCOSM_BYCOPY | SNAPCOSM_RIPPER)) == SNAPCOSM_BYCOPY)
		++g_ceByCopy;
	else if (bAcsClientSide)
		snap_CosPushGeneric(pActor, *pMark, gametic, false);
	else if (snap_CosHeldRow(row))
	{
		// Across its first Tick, and past it while a server script runs.
		pMark->flags |= SNAPCOSM_HELD;
		pMark->heldTic = gametic;
		g_ceHeld.Push(pActor);
		++g_ceHeldN;
	}
	else
		snap_CosPushGeneric(pActor, *pMark, gametic, false);

	// No server copy past the flush; the mark goes with it in Destroy.
	if (((row == SNAPCOS_ROW_GONE) || bAcsClientSide) && snap_CosActs())
	{
		++g_ceRemoved;
		pActor->Destroy();
	}
}

void zx::netsnapshot::NoteFirstTickDone(DThinker* pThinker)
{
	if ((g_cosMarks.CountUsed() == 0) || NETWORK_InClientMode())
		return;
	SnapCosMark* pMark = g_cosMarks.CheckKey(pThinker);
	if ((pMark == NULL) || ((pMark->flags & SNAPCOSM_HELD) == 0) || (pMark->flags & SNAPCOSM_SCRIPT))
		return;
	++g_ceRelTick;
	snap_CosRelease(static_cast<AActor*>(pThinker), pMark, gametic + 1);
}

void zx::netsnapshot::NoteScriptStart(AActor* pActivator, bool bClientSide)
{
	if (NETWORK_GetState() != NETSTATE_SERVER)
		return;
	if ((pActivator == NULL) || bClientSide || (g_cosMarks.CountUsed() == 0))
		return;
	SnapCosMark* pMark = g_cosMarks.CheckKey(pActivator);
	if ((pMark != NULL) && (((pMark->flags & SNAPCOSM_FLUSHED) == 0) || (pMark->flags & SNAPCOSM_HELD)))
		pMark->flags |= SNAPCOSM_SCRIPT;
}

// Finished or paused; before the flush or inside the first Tick, the release at
// that Tick's end carries what it did.
void zx::netsnapshot::NoteScriptStopped(AActor* pActivator)
{
	if (NETWORK_GetState() != NETSTATE_SERVER)
		return;
	if ((pActivator == NULL) || (g_cosMarks.CountUsed() == 0))
		return;
	SnapCosMark* pMark = g_cosMarks.CheckKey(pActivator);
	if ((pMark == NULL) || ((pMark->flags & SNAPCOSM_SCRIPT) == 0))
		return;
	const bool bFirstTick = (g_pSnapTickSelf == pActivator) && (pActivator->ObjectFlags & OF_JustSpawned);
	if (((pMark->flags & SNAPCOSM_HELD) == 0) || bFirstTick)
	{
		pMark->flags &= ~SNAPCOSM_SCRIPT;
		return;
	}
	++g_ceRelScript;
	snap_CosRelease(pActivator, pMark, gametic + 1);
}

// The client copy never sees it.
void zx::netsnapshot::NoteScriptChange(AActor* pActor)
{
	if (NETWORK_GetState() != NETSTATE_SERVER)
		return;
	if ((pActor == NULL) || (g_cosMarks.CountUsed() == 0))
		return;
	const SnapCosMark* pMark = g_cosMarks.CheckKey(pActor);
	if ((pMark != NULL) && (pMark->flags & SNAPCOSM_FLUSHED) && ((pMark->flags & SNAPCOSM_HELD) == 0))
		++g_ceScriptAfter;
}

void zx::netsnapshot::NoteCosmeticRecord(unsigned int id, unsigned int bytes)
{
	if ((g_cosNetRow == NULL) || (id >= SNAP_MAXID) || (fua_snap_cosmetic_events <= 0))
		return;
	if (g_cmByNetID[id] == 1)
	{
		++g_cmTodayRecs;
		g_cmTodayBytes += bytes;
	}
	else if (g_cmByNetID[id] == 2)
	{
		++g_cmSolidRecs;
		g_cmSolidBytes += bytes;
	}
	const BYTE row = g_cosNetRow[id];
	if (row == SNAPCOS_ROW_NONE)
		return;
	++g_ceTodayRecs[row];
	g_ceTodayBytes[row] += bytes;
}

void zx::netsnapshot::NoteMapThing(AActor* pActor)
{
	if ((pActor == NULL) || (pActor->NetID == 0) || (snap_CosActive() == false))
		return;
	snap_CosAlloc();
	snap_CosEnsure();
	++g_cmNetworked;
	const unsigned int id = pActor->GetClass()->getActorNetworkIndex();
	if ((id == 0) || (id >= SNAP_MAXID))
		return;
	if (g_cosFlags[id] & SNAPCOSF_MARKER)
	{
		++g_cmRefused[0];
		return;
	}
	if (g_cosVerdict[id] != SNAPCOS_INERT)
	{
		if (g_cosFlags[id] & SNAPCOSF_SOLIDONLY)
		{
			++g_cmSolidOnly;
			++g_cmSolidByClass[id];
			g_cmByNetID[pActor->NetID] = 2;
		}
		return;
	}
	// Flagged map things stay as stock (neither has a NetID here anyway).
	if (g_cosFlags[id] & (SNAPCOSF_CLIENTSIDE | SNAPCOSF_NONETID))
		return;
	const int why = (pActor->tid != 0) ? 1 : (pActor->special != 0) ? 2 : snap_CosHasArgs(pActor) ? 3 : -1;
	if (why >= 0)
	{
		++g_cmRefused[why];
		return;
	}
	++g_cmListed;
	++g_cmLive;
	++g_cmListedByClass[id];
	g_cmByNetID[pActor->NetID] = 1;
}

// Routed copies past their flush and not held, which a joiner is not told (row
// 1's only in mode 1, which keeps them).
static unsigned int snap_CosCountAlive(unsigned int* pPast5, unsigned int* pPast30, unsigned int* pTidNow)
{
	unsigned int n = 0;
	TMapIterator<DThinker*, SnapCosMark> it(g_cosMarks);
	TMap<DThinker*, SnapCosMark>::Pair* pPair;
	while (it.NextPair(pPair))
	{
		const SnapCosMark& mark = pPair->Value;
		if (((mark.flags & SNAPCOSM_FLUSHED) == 0) || (mark.flags & SNAPCOSM_HELD)
			|| (pPair->Key->ObjectFlags & OF_EuthanizeMe))
			continue;
		++n;
		const AActor* pActor = static_cast<const AActor*>(pPair->Key);
		const int age = gametic - mark.spawnTic;
		if ((pPast5 != NULL) && (age >= 5 * TICRATE))
			++*pPast5;
		if ((pPast30 != NULL) && (age >= 30 * TICRATE))
		{
			++*pPast30;
			const unsigned int id = pActor->GetClass()->getActorNetworkIndex();
			if (id < SNAP_MAXID)
				++g_ceAgeByClass[id];
		}
		if ((pTidNow != NULL) && (pActor->tid != 0))
			++*pTidNow;
	}
	return n;
}

// "Name n, Name n, Name n" for the three largest counts.
static void snap_CosTop3(const unsigned int* pByClass, char* psz, size_t size)
{
	psz[0] = '\0';
	if (pByClass == NULL)
		return;
	unsigned int topId[3] = { 0, 0, 0 }, topN[3] = { 0, 0, 0 };
	for (unsigned int c = 0; c < SNAP_MAXID; ++c)
	{
		const unsigned int n = pByClass[c];
		for (unsigned int k = 0; k < 3; ++k)
		{
			if (n <= topN[k])
				continue;
			for (unsigned int m = 2; m > k; --m) { topN[m] = topN[m - 1]; topId[m] = topId[m - 1]; }
			topN[k] = n;
			topId[k] = c;
			break;
		}
	}
	size_t at = 0;
	for (unsigned int k = 0; (k < 3) && (topN[k] > 0); ++k)
	{
		const PClass* pType = NETWORK_GetClassFromIdentification((unsigned short)topId[k]);
		const int wrote = mysnprintf(psz + at, size - at, "%s%s %u", (k > 0) ? ", " : "",
			(pType != NULL) ? pType->TypeName.GetChars() : "?", topN[k]);
		if ((wrote <= 0) || ((size_t)wrote >= size - at))
			break;
		at += (size_t)wrote;
	}
}

// The cosmetic set, or with an argument every class whose name contains it,
// networked ones with the reason.
CCMD(fua_snap_cosmetics)
{
	if (NETWORK_GetState() != NETSTATE_SERVER)
	{
		Printf("fua_snap_cosmetics is only available on a server.\n");
		return;
	}
	snap_CosEnsure();
	FString filter = (argv.argc() > 1) ? argv[1] : "";
	filter.ToLower();
	unsigned int nRow[SNAPCOS_ROWS] = { 0 }, nShown = 0, nNoIndex = 0;
	for (unsigned int i = 0; i < PClass::m_Types.Size(); ++i)
	{
		const PClass* pType = PClass::m_Types[i];
		if ((pType == NULL) || (pType->ActorInfo == NULL) || !pType->IsDescendantOf(RUNTIME_CLASS(AActor)))
			continue;
		const unsigned int id = pType->getActorNetworkIndex();
		if ((id == 0) || (id >= SNAP_MAXID))
		{
			++nNoIndex;		// no network index: never on the wire
			continue;
		}
		const BYTE row = snap_CosRow(id);
		++nRow[row];

		FString name = pType->TypeName.GetChars();
		name.ToLower();
		const bool bShow = (filter.Len() > 0) ? (name.IndexOf(filter) >= 0) : (row != SNAPCOS_ROW_NONE);
		if (bShow == false)
			continue;
		++nShown;
		const BYTE fl = g_cosFlags[id];
		Printf("  %s: row %u (%s)%s%s%s%s, %s %s\n", pType->TypeName.GetChars(), (unsigned int)row, g_cosRowNames[row],
			(fl & SNAPCOSF_CLIENTSIDE) ? ", +CLIENTSIDEONLY" : "", (fl & SNAPCOSF_NONETID) ? ", +NONETID" : "",
			(fl & SNAPCOSF_MISSILE) ? ", relaxed missile" : "", (fl & SNAPCOSF_ACSNAMED) ? ", ACS-named" : "",
			(g_cosWhy[id] != NULL) ? g_cosWhy[id] : "?", (g_cosWhat[id] != NULL) ? g_cosWhat[id] : "");
	}
	Printf("fua_snap_cosmetics: %u no server copy, %u server copy kept, %u inert, %u plain copy, %u networked,"
		" %u without a network index; %u listed\n", nRow[SNAPCOS_ROW_GONE], nRow[SNAPCOS_ROW_KEPT],
		nRow[SNAPCOS_ROW_INERT], nRow[SNAPCOS_ROW_PLAIN], nRow[SNAPCOS_ROW_NONE], nNoIndex, nShown);
}

// Pump phase timing, on the tic base's monotonic source.
static QWORD		g_phaseCaptureRaw = 0;	// world diff + the capture census
static QWORD		g_phaseSendRaw = 0;		// per-client priority pass and emit
static QWORD		g_phaseTickRaw = 0;		// both, plus the rest of ServerTick
static QWORD		g_phaseWorstRaw = 0;	// worst single tick in the window

// The wall gap between pumps and the report's cost, for stalls the phases miss.
static tictime_raw_t	g_lastPumpRaw = 0;
static int				g_lastPumpGametic = 0;
static QWORD			g_pumpGapWorstRaw = 0;
static int				g_pumpGapWorstTics = 0;
static QWORD			g_pumpGapSumRaw = 0;
static unsigned int		g_pumpGapCount = 0;
// Reset inside the report and sampled after it, so at print time this holds
// the duration of the PREVIOUS report - the current one has not finished.
static QWORD			g_reportRaw = 0;
static unsigned int	g_phaseFrames = 0;

// The report, printed a line a pump, since printed whole it takes longer than
// a tic and the catch-up moves every entity at once.
static TArray<FString>	g_reportQueue;
static unsigned int		g_reportSkipped = 0;

// The report drain, in place of Printf for anything printing inside the tic
// loop, formatting at once so the resets that follow are safe.
static void snap_Say(const char* fmt, ...) GCCPRINTF(1, 2);
static void snap_Say(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	FString s;
	s.VFormat(fmt, ap);
	va_end(ap);
	g_reportQueue.Push(s);
}

static double snap_RawToMS(QWORD raw)
{
	return (double)(QWORD)TIC_ScaleRaw((tictime_raw_t)raw, 1000000) / 1000.0;
}

static void snap_ReportLegacyTraffic(double seconds);

// Chunks of the current client's payload, reused across clients and frames.
static TArray<SnapChunk> g_chunks;
static QWORD		g_chunkDatagrams = 0;
static QWORD		g_chunkFrames = 0;

// Link impairment in the protocol, per client, for tests without an emulator.
CVAR(Int, fua_snap_debug_ackdelay, 0, CVAR_NOSETBYACS)	// frames to hold each ack
CVAR(Int, fua_snap_debug_loss, 0, CVAR_NOSETBYACS)		// percent of packets to drop
// Tics to delay DestroyAllSectorMovers after a map reset (0 off), a second
// reset inside the delay restarting it.
CVAR(Int, fua_snap_debug_resetsplit, 0, CVAR_NOSETBYACS)
static int g_resetSplitDue = 0;			// gametic to send it at; 0 = none pending

namespace {
struct SnapDelayedAck
{
	unsigned int seq;			// the datagram the client acknowledged
	unsigned int history;		// bitmask of the 32 frames before it
	unsigned int dueFrame;		// when the server is allowed to notice
};
}
static TArray<SnapDelayedAck> g_delayedAcks[MAXPLAYERS];
static unsigned int g_droppedPackets = 0;

// Percent of snapshot datagrams held back and sent fua_snap_debug_holdbackgap
// later, the reproducer for a datagram arriving after it was declared lost.
CVAR(Int, fua_snap_debug_holdback, 0, CVAR_NOSETBYACS)
#define SNAP_HOLDBACK_DATAGRAMS		12
// Datagrams a held one waits, 1 to 64: 12 is past the reorder window and
// inside the ack history, past 32 beyond it.
CVAR(Int, fua_snap_debug_holdbackgap, SNAP_HOLDBACK_DATAGRAMS, CVAR_NOSETBYACS)
// Seconds between dumps of what each client may hold to snappresence-sv.txt,
// for snap_tools/presence_diff.py (0 off).
CVAR(Int, fua_snap_debug_presencelog, 0, CVAR_NOSETBYACS)
#define SNAP_HOLDBACK_MAX			16		// per client; past it a datagram goes out on time
#define SNAP_HOLDBACK_SLOT			8192	// bytes a held payload may take, OnSnapshot's limit

// A snapshot datagram's header, as built, so a held one goes out as it was.
namespace {
struct SnapDatagramHeader
{
	SDWORD	frame;
	SDWORD	gameTic;
	SDWORD	seq;
	BYTE	mapStamp;
	BYTE	resetCount;
	BYTE	ackLag;
	WORD	reliable;
};
struct SnapHeldDatagram
{
	SnapDatagramHeader	header;
	unsigned int		releaseSeq;	// sent once the client's datagrams reach it
	unsigned int		slot;		// its payload's slot in g_heldBytes
	unsigned int		length;
};
}
static TArray<SnapHeldDatagram> g_heldDatagrams[MAXPLAYERS];
// SNAP_HOLDBACK_MAX payload slots a client, made on its first hold.
static BYTE* g_heldBytes[MAXPLAYERS];
static unsigned int g_heldPackets = 0;

// One snapshot datagram's command, queued for the client.
static void snap_SendDatagram(unsigned int client, const SnapDatagramHeader& h,
	const BYTE* payload, unsigned int length)
{
	NetCommand command(SVC2_SNAPSHOT);
	command.addLong(h.frame);
	command.addLong(h.gameTic);
	command.addLong(h.seq);
	command.addByte(h.mapStamp);
	command.addByte(h.resetCount);
	command.addByte(h.ackLag);
	command.addShort((int)h.reliable);
	command.addShort((int)length);
	command.addBuffer(payload, length);
	command.setUnreliable(true);
	command.sendCommandToOneClient(client);
	NoteSnapshotQueued(client);
}

// Whether one of the client's held datagrams has its payload in `slot`.
static bool snap_HeldSlotUsed(unsigned int client, unsigned int slot)
{
	for (unsigned int k = 0; k < g_heldDatagrams[client].Size(); ++k)
	{
		if (g_heldDatagrams[client][k].slot == slot)
			return true;
	}
	return false;
}

// Sends the held datagrams whose time has come, each in a datagram of its own,
// once the client's next datagram is `seq`.
static void snap_ReleaseHeldDatagrams(unsigned int client, unsigned int seq)
{
	TArray<SnapHeldDatagram>& held = g_heldDatagrams[client];
	for (unsigned int k = 0; k < held.Size(); )
	{
		if (held[k].releaseSeq > seq)
		{
			++k;
			continue;
		}
		snap_SendDatagram(client, held[k].header,
			g_heldBytes[client] + held[k].slot * SNAP_HOLDBACK_SLOT, held[k].length);
		SERVER_SendClientPacket(client, false);
		held.Delete(k);
	}
}

//*****************************************************************************
//	BRANCH DETECTION AND CENSUS
//
// The detector behind every SNAPEV_BRANCH event, and its census of branches.
static FState** g_prevAnimState = NULL;
// Phase at the previous capture, telling a state entered again from one held.
static int* g_prevAnimTics = NULL;
// Which actor the two above describe, or a recycled id's same-class occupant
// reads as a re-entry.
static AActor** g_prevAnimActor = NULL;
static unsigned int g_reentryCount = 0;
static unsigned int g_reentryReuse = 0;	// slots whose occupant changed
static BYTE* g_branchedTic = NULL;		// 1 = this id branched on the current tic
static unsigned int g_branchCount = 0;	// world-wide, once per branch per tic

// Per-client observations of those branches (about N x g_branchCount at N
// clients), since distance and staleness are client facts.
static unsigned int g_brDist[6];	// <512, 1k, 2k, 4k, 8k, beyond
static unsigned int g_brStale[5];	// <2, <5, <10, <35, 35+ tics since position
static unsigned int g_brVel[3];		// still, <8 units/tic, 8+
static unsigned int g_brDeaths = 0;
static unsigned int g_brDeathsNear = 0;	// deaths inside 512 units
static unsigned int g_brTotal = 0;

//*****************************************************************************
//	STATE CHAIN WALK
//
// The one walker, for the client's animation and appearance suppression alike.

#define SNAP_WALK_MAX_STEPS	64	// chains are cyclic; zero-tic states free-run

// Inverse of the qstate encoder above.
FState * zx::netsnapshot::StateFromQState(const PClass * pType, WORD qstate)
{
	if ((pType == NULL) || (qstate == SNAP_STATE_UNKNOWN))
		return NULL;

	const unsigned int depth = (unsigned int)(qstate >> SNAP_STATE_DEPTH_SHIFT);
	const unsigned int off = (unsigned int)(qstate & SNAP_STATE_OFFSET_MASK);
	for (unsigned int i = 0; (i < depth) && (pType != NULL); ++i)
		pType = pType->ParentClass;

	if ((pType == NULL) || (pType->ActorInfo == NULL))
		return NULL;
	if ((int)off >= pType->ActorInfo->NumOwnedStates)
		return NULL;
	return pType->ActorInfo->OwnedStates + off;
}

// The inverse of StateFromQState, the one encoder.
WORD zx::netsnapshot::QStateForState(const PClass * pType, FState * pState)
{
	if ((pType == NULL) || (pState == NULL))
		return SNAP_STATE_UNKNOWN;

	for (unsigned int depth = 0; (depth <= SNAP_STATE_MAX_DEPTH) && (pType != NULL);
		++depth, pType = pType->ParentClass)
	{
		if ((pType->ActorInfo == NULL) || !pType->ActorInfo->OwnsState(pState))
			continue;
		const ptrdiff_t off = pState - pType->ActorInfo->OwnedStates;
		if ((off >= 0) && (off <= SNAP_STATE_OFFSET_MASK))
			return (WORD)((depth << SNAP_STATE_DEPTH_SHIFT) | (WORD)off);
		break;
	}
	return SNAP_STATE_UNKNOWN;
}

// Walks exactly `elapsedTics` and reports where the chain lands, reading tics
// raw (GetTics draws on the RNG) and flagging random and Fast/Slow states.
FState * zx::netsnapshot::WalkChain(FState * pFrom, int ticsRemaining, int elapsedTics,
	int * pFlags)
{
	int flags = 0;
	int steps = 0;
	if (pFrom != NULL)
	{
		int remaining = ticsRemaining;
		int left = (elapsedTics > 0) ? elapsedTics : 0;
		for (; steps < SNAP_WALK_MAX_STEPS; ++steps)
		{
			if (remaining < 0)
			{
				flags |= SNAP_WALK_HELD;
				break;
			}
			if (left < remaining)
				break;				// lands inside this state
			left -= remaining;
			FState * const pNext = pFrom->GetNextState();
			if (pNext == NULL)
			{
				pFrom = NULL;		// chain ends; there is no landing state
				break;
			}
			pFrom = pNext;
			if (pFrom->TicRange != 0)
				flags |= SNAP_WALK_RNG;
			if (pFrom->Fast || pFrom->Slow)
				flags |= SNAP_WALK_SCALED;
			remaining = (pFrom->Tics < 0) ? -1 : (int)pFrom->Tics;
		}
	}
	if (steps >= SNAP_WALK_MAX_STEPS)
		flags |= SNAP_WALK_CAPPED;
	if (pFlags != NULL)
		*pFlags = flags;
	return pFrom;
}

// Whether a client animating unaided from what it was told is on the server's
// state now: derivable only when the walk lands exactly there by a route the
// client reproduces, with no random, scaled, held or capped step.
int zx::netsnapshot::AppearDerivable(WORD classId, WORD wasQState, BYTE wasQTics,
	int elapsedTics, WORD curQState)
{
	if ((wasQState == SNAP_STATE_UNKNOWN) || (curQState == SNAP_STATE_UNKNOWN))
		return SNAP_APPEAR_UNWALKABLE;

	// 255 is "unknown or infinite" - the phase the walk would start from is
	// exactly what is missing, so there is nothing to walk.
	if (wasQTics == 255)
		return SNAP_APPEAR_UNWALKABLE;

	if ((elapsedTics <= 0) || (elapsedTics > SNAP_APPEAR_MAX_TICS))
		return SNAP_APPEAR_UNWALKABLE;

	const PClass * const pType = NETWORK_GetClassFromIdentification(classId);
	if (pType == NULL)
		return SNAP_APPEAR_UNWALKABLE;

	FState * const pFrom = StateFromQState(pType, wasQState);
	if (pFrom == NULL)
		return SNAP_APPEAR_UNWALKABLE;

	int flags = 0;
	FState * const pLand = WalkChain(pFrom, (int)wasQTics, elapsedTics, &flags);
	if ((pLand == NULL) || (flags != 0))
		return SNAP_APPEAR_UNWALKABLE;

	return (QStateForState(pType, pLand) == curQState)
		? SNAP_APPEAR_DERIVABLE : SNAP_APPEAR_DIVERGED;
}

// This tic's psprite state per player slot, captured once for every client.
static SnapPSpriteState g_psprCapture[MAXPLAYERS];

const SnapPSpriteState * zx::netsnapshot::PSpriteCapture()
{
	return g_psprCapture;
}

WORD zx::netsnapshot::PlayerReservedId(unsigned int playernum)
{
	if (playernum >= MAXPLAYERS)
		return 0;
	return (WORD)IDList<AActor>::reservedIDForPlayer((int)playernum);
}

// Missiles a player fired and their spawn tics, in a ring whose overwrites are
// counted, for the own-missile census the client's fire census is read against.
#define SNAP_OWNMISSILE_RING	64

namespace {
struct SnapOwnMissile
{
	WORD			id;
	BYTE			player;
	int				spawnTic;
	// The clients already sent an enter for this missile, so a re-send is not
	// counted (two words for MAXPLAYERS 64).
	unsigned int	sentTo[2];
};
}
static SnapOwnMissile g_ownMissile[SNAP_OWNMISSILE_RING];
static unsigned int g_ownMissileHead = 0;
static unsigned int g_ownMissileSpawns = 0;
static unsigned int g_ownMissileEvicted = 0;
static unsigned int g_ownMissileResent = 0;

static void snap_NoteOwnMissile(WORD id, unsigned int player, int spawnTic)
{
	// Overwriting a slot is an eviction only if it holds a missile (id 0
	// is cleared) that nobody has looked up.
	SnapOwnMissile & e = g_ownMissile[g_ownMissileHead];
	if ((e.id != 0) && ((e.sentTo[0] | e.sentTo[1]) == 0))
		++g_ownMissileEvicted;
	e.id = id;
	e.player = (BYTE)player;
	e.spawnTic = spawnTic;
	e.sentTo[0] = e.sentTo[1] = 0;
	g_ownMissileHead = (g_ownMissileHead + 1) % SNAP_OWNMISSILE_RING;
	++g_ownMissileSpawns;
}

// True only on the first enter emitted to this client for this missile, a
// re-send counted and refused.
bool zx::netsnapshot::OwnMissileFirstEnter(WORD id, unsigned int player, int & spawnTic)
{
	if ((id == 0) || (player >= MAXPLAYERS))
		return false;
	for (unsigned int i = 0; i < SNAP_OWNMISSILE_RING; ++i)
	{
		SnapOwnMissile & e = g_ownMissile[i];
		if ((e.id != id) || (e.player != (BYTE)player))
			continue;
		const unsigned int w = player >> 5;
		const unsigned int b = 1u << (player & 31);
		if (e.sentTo[w] & b)
		{
			++g_ownMissileResent;
			return false;
		}
		e.sentTo[w] |= b;
		spawnTic = e.spawnTic;
		return true;
	}
	return false;
}

void zx::netsnapshot::OwnMissileStats(unsigned int & spawns, unsigned int & evicted,
	unsigned int & resent, bool reset)
{
	spawns = g_ownMissileSpawns;
	evicted = g_ownMissileEvicted;
	resent = g_ownMissileResent;
	if (reset)
		g_ownMissileSpawns = g_ownMissileEvicted = g_ownMissileResent = 0;
}

// The local player's authoritative block, captured for ONE client: per
// RECIPIENT, since only the player a block describes can use it. clientTic
// is the client's own gametic, not the server's: it names the prediction
// ring slot the position belongs to, and nothing else on the snapshot
// channel carries it.
int zx::netsnapshot::LocalCapture(unsigned int playernum, SnapLocalState & out)
{
	memset(&out, 0, sizeof(out));
	if (playernum >= MAXPLAYERS)
		return SNAPLOCAL_CAP_NOPAWN;
	if ((playeringame[playernum] == false) || (players[playernum].mo == NULL))
		return SNAPLOCAL_CAP_NOPAWN;
	// A spectator's position is client-owned, so it gets no block, as
	// sv_main.cpp's MoveLocalPlayer skips it.
	if (players[playernum].bSpectating)
		return SNAPLOCAL_CAP_SPECTATOR;
	const CLIENT_s * const pClient = SERVER_GetClient(playernum);
	if (pClient == NULL)
		return SNAPLOCAL_CAP_NOCLIENT;

	const AActor * const pMo = players[playernum].mo;
	// The bit the client applies on, from the legacy commands' gate.
	out.flags = SuppressesLegacyLocal(playernum)
		? (BYTE)SNAPLOCAL_AUTHORITATIVE : (BYTE)0;
	out.clientTic = (DWORD)pClient->ulClientGameTic;
	out.x = pMo->x;
	out.y = pMo->y;
	out.z = pMo->z;
	out.velx = pMo->velx;
	out.vely = pMo->vely;
	out.velz = pMo->velz;
	out.jumpTics = (SWORD)players[playernum].jumpTics;
	return SNAPLOCAL_CAP_OK;
}

// qstate resolves against ReadyWeapon's class, not the pawn's: psprite
// states belong to the weapon.
static void snap_CapturePSprites()
{
	for (unsigned int i = 0; i < MAXPLAYERS; ++i)
	{
		SnapPSpriteState & out = g_psprCapture[i];
		memset(&out, 0, sizeof(out));
		out.qstate[0] = out.qstate[1] = SNAP_STATE_UNKNOWN;
		out.qtics[0] = out.qtics[1] = 255;

		if ((playeringame[i] == false) || (players[i].mo == NULL))
			continue;
		const AWeapon * const pWeapon = players[i].ReadyWeapon;
		if (pWeapon == NULL)
			continue;

		const PClass * const pType = pWeapon->GetClass();
		out.weaponClass = (WORD)pType->getActorNetworkIndex();

		for (unsigned int L = 0; L < 2; ++L)
		{
			const pspdef_t & psp = players[i].psprites[(L == 0) ? ps_weapon : ps_flash];
			if (psp.state == NULL)
				continue;
			out.qstate[L] = QStateForState(pType, psp.state);
			// Same 255 convention APPEAR uses for an unknown or infinite phase.
			out.qtics[L] = ((psp.tics < 0) || (psp.tics > 254))
				? (BYTE)255 : (BYTE)psp.tics;
		}
	}
}

//*****************************************************************************
//	STRAIGHTNESS TRACKER
//
// How many tics a moving actor's heading holds, which sets the missile bits and
// scores them for the report.
static WORD* g_lastVelDir = NULL;	// BAM >> 16, or 0xFFFF for 'was not moving'
// Z velocity at the previous tic, meaningful only where g_lastVelDir is.
static fixed_t* g_lastVelZ = NULL;
static BYTE* g_straightRun = NULL;	// consecutive tics on the same heading

// Run-length histogram: 1, 2-3, 4-7, 8-15, 16+ tics.
static unsigned int g_runMissile[5];
static unsigned int g_runOther[5];

// Whether the ballistic bit predicted right (missiles only), the previous tic's
// bit scored against whether this tic held its heading.
static unsigned int g_predHeld = 0;		// bit set, heading held: extrapolation was right
static unsigned int g_predTurned = 0;	// bit set, heading turned: extrapolated into a turn
static unsigned int g_stepHeld = 0;		// bit clear, heading held: stepped something straight
static unsigned int g_stepTurned = 0;	// bit clear, heading turned: stepped correctly
static unsigned int g_straightSamples = 0;

// The Z velocity change at the previous tic, for the gravity test.
static fixed_t * g_lastDVelZ = NULL;
static BYTE * g_prevStraightBit = NULL;

// Constant-acceleration runs, beside the straight run since the two extrapolate
// differently and only the straight one is suppressed.
static BYTE* g_gravRun = NULL;

double zx::netsnapshot::LawGravity(const PClass* pType, const sector_t* pSector)
{
	if ((pType == NULL) || (pSector == NULL) || (pType->IsDescendantOf(RUNTIME_CLASS(AActor)) == false))
		return 0.0;
	const AActor* pDef = GetDefaultByType(pType);
	if ((pDef == NULL) || (pDef->flags & MF_NOGRAVITY))
		return 0.0;
	return ComputeGravityAccel(level.gravity, pSector->gravity, pDef->gravity);
}

bool zx::netsnapshot::LawAt(unsigned int classId, fixed_t x, fixed_t y, double& g, bool& bOldZ)
{
	const PClass* pType = NETWORK_GetClassFromIdentification((unsigned short)classId);
	if (pType == NULL)
		return false;
	g = LawGravity(pType, P_PointInSector(x, y));
	bOldZ = (zacompatflags & ZACOMPATF_OLD_ZDOOM_ZMOVEMENT) != 0;
	return true;
}

static bool snap_IsGravStraight(unsigned int id)
{
	return (g_gravRun != NULL) && (id > 0) && (id < SNAP_MAXID)
		&& (g_gravRun[id] >= SNAP_STRAIGHT_MIN);
}

static bool snap_IsStraight(unsigned int id)
{
	return (g_straightRun != NULL) && (id > 0) && (id < SNAP_MAXID)
		&& (g_straightRun[id] >= SNAP_STRAIGHT_MIN);
}

void zx::netsnapshot::StraightReset()
{
	if (g_lastVelDir != NULL)
		memset(g_lastVelDir, 0xFF, SNAP_MAXID * sizeof(WORD));
	if (g_lastVelZ != NULL)
		memset(g_lastVelZ, 0, SNAP_MAXID * sizeof(fixed_t));
	if (g_lastDVelZ != NULL)
		memset(g_lastDVelZ, 0, SNAP_MAXID * sizeof(fixed_t));
	if (g_straightRun != NULL)
		memset(g_straightRun, 0, SNAP_MAXID);
	if (g_prevStraightBit != NULL)
		memset(g_prevStraightBit, 0, SNAP_MAXID);
	if (g_wasMissile != NULL)
		memset(g_wasMissile, 0, SNAP_MAXID);
	if (g_terminalSeq != NULL)
		memset(g_terminalSeq, 0, SNAP_MAXID);
	if (g_lastClassAtId != NULL)
		memset(g_lastClassAtId, 0, SNAP_MAXID * sizeof(WORD));
}

// Set on the tic MF_MISSILE|MF_SKULLFLY clears, then counted down over a death
// sequence, since a latch would flag a lost soul that stops charging for life.
static void snap_NoteTerminal(AActor * pActor)
{
	const unsigned int id = pActor->NetID;
	if ((id == 0) || (id >= SNAP_MAXID))
		return;
	
	if (g_wasMissile == NULL)
	{
		g_wasMissile = new BYTE[SNAP_MAXID];
		memset(g_wasMissile, 0, SNAP_MAXID);
		g_terminalSeq = new BYTE[SNAP_MAXID];
		memset(g_terminalSeq, 0, SNAP_MAXID);
		g_lastClassAtId = new WORD[SNAP_MAXID];
		memset(g_lastClassAtId, 0, SNAP_MAXID * sizeof(WORD));
	}
	// A different class at this id is a different actor: without this the
	// flag survives id reuse and lands on things that never detonated.
	const WORD classNow = (WORD)pActor->GetClass()->getActorNetworkIndex();
	if (g_lastClassAtId[id] != classNow)
	{
		g_lastClassAtId[id] = classNow;
		g_wasMissile[id] = 0;
		g_terminalSeq[id] = 0;
	}

	const BYTE isMissile = (pActor->flags & (MF_MISSILE | MF_SKULLFLY)) ? 1 : 0;
	if (isMissile)
		g_terminalSeq[id] = 0;
	else if (g_wasMissile[id])
		g_terminalSeq[id] = SNAP_TERMINAL_TICS;
	else if (g_terminalSeq[id] > 0)
		--g_terminalSeq[id];
	
	g_wasMissile[id] = isMissile;
}



static void snap_NoteStraightness(AActor* pActor)
{
	if ((pActor == NULL) || (pActor->NetID == 0) || (pActor->NetID >= SNAP_MAXID))
		return;
	if (g_lastVelDir == NULL)
	{
		g_lastVelDir = new WORD[SNAP_MAXID];
		memset(g_lastVelDir, 0xFF, SNAP_MAXID * sizeof(WORD));
		g_lastVelZ = new fixed_t[SNAP_MAXID];
		memset(g_lastVelZ, 0, SNAP_MAXID * sizeof(fixed_t));
		g_lastDVelZ = new fixed_t[SNAP_MAXID];
		memset(g_lastDVelZ, 0, SNAP_MAXID * sizeof(fixed_t));
		g_gravRun = new BYTE[SNAP_MAXID];
		memset(g_gravRun, 0, SNAP_MAXID);
		g_straightRun = new BYTE[SNAP_MAXID];
		memset(g_straightRun, 0, SNAP_MAXID);
		g_prevStraightBit = new BYTE[SNAP_MAXID];
		memset(g_prevStraightBit, 0, SNAP_MAXID);
	}
	const unsigned int id = pActor->NetID;

	// The step (ComputeRunStep), over this id's runs as the arrays hold them.
	SnapRunTrack track;
	track.lastDir = g_lastVelDir[id];
	track.lastVelZ = g_lastVelZ[id];
	track.lastDVelZ = g_lastDVelZ[id];
	track.straightRun = g_straightRun[id];
	track.gravRun = g_gravRun[id];
	const SnapRunStep step = ComputeRunStep(track, pActor->velx, pActor->vely, pActor->velz);
	g_lastVelDir[id] = track.lastDir;
	g_lastVelZ[id] = track.lastVelZ;
	g_lastDVelZ[id] = track.lastDVelZ;
	g_straightRun[id] = track.straightRun;
	g_gravRun[id] = track.gravRun;
	if (step.bMoving == false)
	{
		g_prevStraightBit[id] = 0;
		return;
	}

	// The previous tic's bit scored, for a missile with a heading.
	const bool bMissile = (pActor->flags & MF_MISSILE) != 0;
	if (step.bCompared && bMissile)
	{
		++g_straightSamples;
		if (g_prevStraightBit[id] != 0)
		{
			if (step.bHeld)	++g_predHeld;
			else			++g_predTurned;
		}
		else
		{
			if (step.bHeld)	++g_stepHeld;
			else			++g_stepTurned;
		}
	}
	// The run a turn ended, counted once at its true length.
	if (step.endedBucket >= 0)
	{
		if (bMissile)	++g_runMissile[step.endedBucket];
		else			++g_runOther[step.endedBucket];
	}

	// The bit as it will ship this tic, held for next tic's scoring.
	g_prevStraightBit[id] = snap_IsStraight(id) ? 1 : 0;
}


void zx::netsnapshot::BranchReset()
{
	if (g_prevAnimState != NULL)
		memset(g_prevAnimState, 0, SNAP_MAXID * sizeof(FState*));
	if (g_branchedTic != NULL)
		memset(g_branchedTic, 0, SNAP_MAXID);
}

bool zx::netsnapshot::BranchedThisTic(unsigned int id)
{
	return (g_branchedTic != NULL) && (id < SNAP_MAXID) && (g_branchedTic[id] != 0);
}

// Once per actor per tic in the capture sweep: a branch is an actor not where
// its previous state led, or a state entered again (design doc 5.2).

// How far a zero-duration chain is walked, past nested jump tables.
#define SNAP_ZEROCHAIN_MAX	16

// Whether pFrom's chain reaches pTo through zero-duration states, which the
// capture never sees, bounded since such a chain can be circular.
static bool snap_ChainReaches(FState* pFrom, FState* pTo)
{
	FState* pAt = (pFrom != NULL) ? pFrom->GetNextState() : NULL;
	for (int step = 0; (step < SNAP_ZEROCHAIN_MAX) && (pAt != NULL); ++step)
	{
		if (pAt == pTo)
			return true;
		// A state with a duration is where the actor would have stopped.
		if (pAt->Tics != 0)
			return false;
		pAt = pAt->GetNextState();
	}
	return false;
}

static void snap_NoteBranch(AActor* pActor)
{
	if ((pActor == NULL) || (pActor->NetID == 0) || (pActor->NetID >= SNAP_MAXID))
		return;
	if (g_prevAnimState == NULL)
	{
		g_prevAnimState = new FState*[SNAP_MAXID];
		memset(g_prevAnimState, 0, SNAP_MAXID * sizeof(FState*));
		g_prevAnimTics = new int[SNAP_MAXID];
		memset(g_prevAnimTics, 0, SNAP_MAXID * sizeof(int));
		g_prevAnimActor = new AActor*[SNAP_MAXID];
		memset(g_prevAnimActor, 0, SNAP_MAXID * sizeof(AActor*));
		g_branchedTic = new BYTE[SNAP_MAXID];
		memset(g_branchedTic, 0, SNAP_MAXID);
	}
	const unsigned int id = pActor->NetID;

	// A different actor here is a first sight (compared, not dereferenced).
	if (g_prevAnimActor[id] != pActor)
	{
		g_prevAnimActor[id] = pActor;
		g_prevAnimState[id] = pActor->state;
		g_prevAnimTics[id] = pActor->tics;
		g_branchedTic[id] = 0;
		++g_reentryReuse;
		return;
	}

	FState* pPrev = g_prevAnimState[id];
	FState* pNow = pActor->state;
	const int prevTics = g_prevAnimTics[id];
	g_branchedTic[id] = 0;
	if ((pPrev != NULL) && (pNow != NULL) && (pNow != pPrev)
		&& (snap_ChainReaches(pPrev, pNow) == false))
	{
		g_branchedTic[id] = 1;
		++g_branchCount;
	}
	else if ((pPrev != NULL) && (pNow == pPrev) && (pActor->tics > prevTics)
		&& (snap_ChainReaches(pPrev, pPrev) == false))
	{
		// A re-entry, counted apart from branches, excluding a state that
		// loops to itself, which is the engine's ordinary advance.
		g_branchedTic[id] = 1;
		++g_branchCount;
		++g_reentryCount;
	}
	g_prevAnimState[id] = pNow;
	g_prevAnimTics[id] = pActor->tics;
}

// Called from the per-client pass, where distance and staleness are known.
void zx::netsnapshot::BranchCensus(int distUnits, int staleTics, int velUnitsPerTic, bool bDeath)
{
	++g_brTotal;
	const int d = (distUnits < 512) ? 0 : (distUnits < 1024) ? 1 : (distUnits < 2048) ? 2
		: (distUnits < 4096) ? 3 : (distUnits < 8192) ? 4 : 5;
	++g_brDist[d];
	const int t = (staleTics < 2) ? 0 : (staleTics < 5) ? 1 : (staleTics < 10) ? 2
		: (staleTics < 35) ? 3 : 4;
	++g_brStale[t];
	++g_brVel[(velUnitsPerTic == 0) ? 0 : (velUnitsPerTic < 8) ? 1 : 2];
	if (bDeath)
	{
		++g_brDeaths;
		if (distUnits < 512)
			++g_brDeathsNear;
	}
}


// Hands over each ack whose artificial delay has expired, before the frame is
// built, so the delay acts as latency rather than a stalled client.
static void snap_ReleaseDelayedAcks()
{
	for (unsigned int i = 0; i < MAXPLAYERS; ++i)
	{
		for (unsigned int k = 0; k < g_delayedAcks[i].Size(); )
		{
			if (g_delayedAcks[i][k].dueFrame > g_snapFrame)
			{
				++k;
				continue;
			}
			OnAck(i, g_delayedAcks[i][k].seq, g_delayedAcks[i][k].history);
			g_delayedAcks[i].Delete(k);
		}
	}
}

// Monsters awake and hunting, so a log can be read against how busy the map
// actually was rather than how busy it looked.
static unsigned int	g_huntingMonsters = 0;
static unsigned int	g_liveMonsters = 0;
static QWORD		g_huntingAccum = 0;
static QWORD		g_liveAccum = 0;
// Owned inventory left out of the capture, per frame and summed for the report.
static unsigned int	g_ownedSkipped = 0;
static QWORD		g_ownedAccum = 0;
static unsigned int	g_huntingFrames = 0;
static BYTE* g_prevPresent = NULL;
static BYTE* g_curPresent = NULL;
static WORD* g_idleTics = NULL;
// Which classes are entering, by rate, since enters re-sent over the ack round
// trip are a large share of the budget.
#define SNAP_ENTERCLASS_REPORT		6		// how many classes to name
static unsigned int* g_enterByClass = NULL;		// indexed by classId

// One copy of the band names, shared by every report that names a band; the
// assertion on SNAPCL_TIERS_REPORT catches a missing entry at build time.
static const char* const g_szTierName[SNAPCL_TIERS_REPORT] =
{
	"overdue", "top", "enter", "MISSILE", "d0", "d1",
	"d2", "d3", "d4", "d5", "dlast", "refresh", "animref", "cosmetic"
};

// Population by class, settled ones (unchanged for SNAP_SETTLE_TICS) apart,
// which says whether a slow refresh can help (9.3).
static unsigned int* g_popByClass = NULL;
static unsigned int* g_popSettledByClass = NULL;
static unsigned int  g_popTotal = 0;
static unsigned int  g_popSettledTotal = 0;
static unsigned int  g_enterClassTotal = 0;

static TArray<SnapEntityState> g_capture;
static TArray<BYTE> g_capturePriority;		// parallel to g_capture, server-only
static TArray<BYTE>		g_deltaBuf;
static TArray<BYTE>		g_zstdBuf;

static bool snap_ClientInMap(unsigned int client);



// Accumulated over the reporting window.
namespace {
struct SnapStats
{
	unsigned int frames;
	QWORD simTics;			// gametics covered by these captures; >frames = server saturated
	QWORD entities, awake, settled, enters, leaves;
	QWORD deltaBytes, zstdBytes, dirtyRecords;
	QWORD animOnly;			// dirty records whose only change was SNAPGRP_APPEAR

	// Dirty records and their payload bytes per group, in snap_TallyGroups'
	// order (POS, POSZ, ANG, VELMISC, APPEAR, RENDER).
	QWORD grpRecs[SNAP_GROUP_COUNT];
	QWORD grpBytes[SNAP_GROUP_COUNT];

	unsigned int peakDelta, peakZstd, peakEntities;

	void Reset() { memset(this, 0, sizeof(*this)); }
};
}
static SnapStats g_stats;

// Attributes a dirty record's mask to its groups (not an enter's: all).
static void snap_TallyGroups(BYTE mask, const SnapEntityState& st)
{
	static const BYTE bits[SNAP_GROUP_COUNT] =
	{
		SNAPGRP_POS, SNAPGRP_POSZ, SNAPGRP_ANG, SNAPGRP_VELMISC, SNAPGRP_APPEAR, SNAPGRP_RENDER
	};
	for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
	{
		if (mask & bits[g])
		{
			++g_stats.grpRecs[g];
			g_stats.grpBytes[g] += GroupBytesFor(ServerMapFrame(), bits[g], st);
		}
	}
}
static int g_lastCaptureTic = -1;
static int g_lastReportTic = 0;

//*****************************************************************************
//	STREAM RECORDER (design doc appendix B)
//
// Every capture to a .zsnap file, from a full baseline, so server traffic
// replays through the client's decoder; one recording, one map.

static FILE* g_recordFile = NULL;
static unsigned int	g_recordRecords = 0;
static QWORD		g_recordBytes = 0;

static void snap_RecordWrite32(DWORD v) { fwrite(&v, 4, 1, g_recordFile); }
static void snap_RecordWrite16(WORD v) { fwrite(&v, 2, 1, g_recordFile); }

static void snap_RecordStop(const char* pszReason)
{
	if (g_recordFile == NULL)
		return;

	fclose(g_recordFile);
	g_recordFile = NULL;
	Printf("fua_snap_record: stopped (%s). %u records, %.1f MB written.\n",
		pszReason, g_recordRecords, (double)g_recordBytes / (1024.0 * 1024.0));
}

static void snap_RecordCapture(unsigned int spanTics, const TArray<BYTE>& raw, unsigned int zstdSize)
{
	if (g_recordFile == NULL)
		return;

	// Store whichever form is smaller; the reader distinguishes by size.
	const bool storeCompressed = (zstdSize > 0) && (zstdSize < raw.Size());
	const unsigned int storedSize = storeCompressed ? zstdSize : raw.Size();

	snap_RecordWrite32((DWORD)gametic);
	snap_RecordWrite32((DWORD)I_MSTime());
	snap_RecordWrite16((WORD)(spanTics > 0xFFFF ? 0xFFFF : spanTics));
	snap_RecordWrite32(raw.Size());
	snap_RecordWrite32(storedSize);

	if (storedSize > 0)
	{
		const BYTE* pPayload = storeCompressed ? &g_zstdBuf[0] : &raw[0];
		if (fwrite(pPayload, 1, storedSize, g_recordFile) != storedSize)
		{
			snap_RecordStop("write failed - disk full?");
			return;
		}
	}

	++g_recordRecords;
	g_recordBytes += 18 + storedSize;
}

//*****************************************************************************
//	INITIALIZATION

// Every snapshot cvar not at its default, skipping CVAR_AUTO ones, the ini's
// placeholders for names this build no longer has.
void zx::netsnapshot::ReportCvars()
{
	FString line;
	unsigned int n = 0;
	for (FBaseCVar* pVar = CVars; pVar != NULL; pVar = pVar->GetNext())
	{
		if (pVar->GetFlags() & CVAR_AUTO)
			continue;
		const char* pszName = pVar->GetName();
		const bool bServer = (strncmp(pszName, "fua_snap", 8) == 0)
			&& ((pszName[8] == '\0') || (pszName[8] == '_'));
		if ((bServer == false) && (strncmp(pszName, "fua_csnap_", 10) != 0))
			continue;
		// Copied: the string form of a numeric cvar lives in one static buffer.
		const UCVarValue nowRep = pVar->GetGenericRep(CVAR_String);
		const FString now = (nowRep.String != NULL) ? nowRep.String : "";
		const UCVarValue defRep = pVar->GetGenericRepDefault(CVAR_String);
		const FString def = (defRep.String != NULL) ? defRep.String : "";
		if (now.Compare(def) == 0)
			continue;
		line.AppendFormat("%s%s=%s (%s)", (n > 0) ? ", " : "", pszName, now.GetChars(), def.GetChars());
		++n;
	}
	if (n > 0)
		Printf("[snap] cvars off default: %s\n", line.GetChars());
	else
		Printf("[snap] cvars: all at default\n");
}

static void snap_Init()
{
	if (g_snapInitialized)
		return;

	ReportCvars();
	// Class indices are contiguous from 1, so the bit's own index existing
	// means one reaches it; the writer would then alias it to a target.
	if (NETWORK_GetClassFromIdentification((USHORT)SNAP_CLASS_TARGET) != NULL)
		I_Error("snapshot: an actor class index reaches SNAP_CLASS_TARGET (%u)", (unsigned int)SNAP_CLASS_TARGET);

	g_prevState = new SnapEntityState[SNAP_MAXID];
	g_prevPresent = new BYTE[SNAP_MAXID];
	g_curPresent = new BYTE[SNAP_MAXID];
	g_idleTics = new WORD[SNAP_MAXID];
	g_targetAtId = new WORD[SNAP_MAXID];
	memset(g_targetAtId, 0, SNAP_MAXID * sizeof(WORD));
	// classId is an actor network index, bounded by the same 16-bit space the
	// wire format uses for entity ids.
	g_enterByClass = new unsigned int[SNAP_MAXID];
	memset(g_enterByClass, 0, SNAP_MAXID * sizeof(unsigned int));
	g_popByClass = new unsigned int[SNAP_MAXID];
	memset(g_popByClass, 0, SNAP_MAXID * sizeof(unsigned int));
	g_popSettledByClass = new unsigned int[SNAP_MAXID];
	memset(g_popSettledByClass, 0, SNAP_MAXID * sizeof(unsigned int));
	memset(g_prevPresent, 0, SNAP_MAXID);
	memset(g_curPresent, 0, SNAP_MAXID);
	memset(g_idleTics, 0, SNAP_MAXID * sizeof(WORD));
	g_stats.Reset();
	g_snapInitialized = true;
}

// The capture payload's id base, reset with the buffer and nowhere else.
static SnapIdCursor g_deltaIds;

// Append one record to the delta buffer using the shared codec (format.h),
// writing straight into the buffer with no intermediate copy.
static void snap_AppendRecord(WORD id, BYTE mask, WORD classId, const SnapEntityState& st,
	WORD targetId = 0)
{
	const unsigned int base = g_deltaBuf.Size();
	g_deltaBuf.Resize(base + SNAP_MAX_RECORD_BYTES);
	const unsigned int n = WriteRecord(&g_deltaBuf[base], ServerMapFrame(),
		id, g_deltaIds, mask, classId, st, NULL, targetId);
	g_deltaBuf.Resize(base + n);
}

static void snap_SendToClients();

//*****************************************************************************
//	THE CAPTURE PASS

static void snap_CaptureAndMeasure()
{
	// Sweep every net-visible actor into this frame's capture set.
	g_capture.Clear();
	g_capturePriority.Clear();
	g_huntingMonsters = 0;
	g_liveMonsters = 0;
	g_ownedSkipped = 0;
	{
		TThinkerIterator<AActor> iterator;
		AActor* pActor;
		while ((pActor = iterator.Next()) != NULL)
		{
			if (pActor->NetID == 0)
				continue;

			// A spectator's pawn is left out, since a mirror would draw it as a
			// body; spectating is a leave and unspectating an enter.
			if ((pActor->player != NULL) && pActor->player->bSpectating)
				continue;

			// Owned inventory reaches the client through the inventory
			// commands, so a pickup is a leave and a drop an enter.
			if (pActor->IsKindOf(RUNTIME_CLASS(AInventory))
				&& (static_cast<AInventory *>(pActor)->Owner != NULL))
			{
				++g_ownedSkipped;
				continue;
			}

			// Before capture, so both compare against what this actor held on
			// the PREVIOUS tic.
			snap_NoteBranch(pActor);
			snap_NoteStraightness(pActor);

			SnapEntityState state;
			snap_CaptureActor(pActor, state);
			g_capture.Push(state);

			// See g_huntingMonsters: a live MF_COUNTKILL actor holding a
			// target is awake and hunting something.
			if ((pActor->flags & MF_COUNTKILL) && (pActor->health > 0))
			{
				++g_liveMonsters;
				if (pActor->target != NULL)
					++g_huntingMonsters;
			}

			// Server-side priority hints, deciding who gets a scarce budget.
			BYTE pri = 0;
			if (pActor->player != NULL)
				pri |= SNAPPRI_PLAYER;

			snap_NoteTerminal(pActor);
			if ((pActor->NetID < SNAP_MAXID) && (g_terminalSeq != NULL)
				&& g_terminalSeq[pActor->NetID])
				pri |= SNAPPRI_TERMINAL;

			if (pActor->flags & (MF_MISSILE | MF_SKULLFLY))
			{
				pri |= SNAPPRI_MISSILE;
				// The spawn tic of a missile a player fired, entering while
				// g_prevPresent, the previous frame's, reads 0.
				if ((pActor->target != NULL) && (pActor->target->player != NULL)
					&& (pActor->NetID != 0) && (pActor->NetID < SNAP_MAXID)
					&& (g_prevPresent != NULL) && (g_prevPresent[pActor->NetID] == 0))
				{
					snap_NoteOwnMissile((WORD)pActor->NetID,
						(unsigned int)(pActor->target->player - players), gametic);
				}
				// Steering, observed as the misc bit is: a projectile only a
				// frequent refresh helps, unless still or under gravity.
				const bool steers = (snap_IsStraight(pActor->NetID) == false)
					&& (snap_IsGravStraight(pActor->NetID) == false)
					&& ((QuantVel(pActor->velx) | QuantVel(pActor->vely)
						| QuantVel(pActor->velz)) != 0);
				if (steers)
					pri |= SNAPPRI_HOMING;
			}
			g_capturePriority.Push(pri);
		}
	}

	memset(g_curPresent, 0, SNAP_MAXID);

	g_huntingAccum += g_huntingMonsters;
	g_liveAccum += g_liveMonsters;
	g_ownedAccum += g_ownedSkipped;
	++g_huntingFrames;

	// Delta this frame against the previous one, writing real bytes.
	g_deltaBuf.Clear();
	IdCursorReset(g_deltaIds);
	unsigned int awake = 0, settled = 0, enters = 0, dirtyRecords = 0, animOnly = 0;

	for (unsigned int i = 0; i < g_capture.Size(); ++i)
	{
		const SnapEntityState& cur = g_capture[i];
		g_curPresent[cur.id] = 1;

		if (g_prevPresent[cur.id] == 0)
		{
			// Enter: id + mask + class + all groups.
			++enters;
			++dirtyRecords;
			if ((g_enterByClass != NULL) && (cur.classId < SNAP_MAXID))
			{
				++g_enterByClass[cur.classId];
				++g_enterClassTotal;
			}
			const WORD target = EnterTarget(cur.id);
			if (target != 0)
				++g_targetEnters;
			else if (MissileFlagged(cur.misc)
				&& ClassSpawns(NETWORK_GetClassFromIdentification(cur.classId)))
				++g_targetNoneEnters;
			snap_AppendRecord(cur.id, SNAPGRP_ALL | SNAPGRP_ENTER, cur.classId, cur, target);
			g_idleTics[cur.id] = 0;
		}
		else
		{
			const BYTE mask = DiffMask(g_prevState[cur.id], cur);
			if (mask != 0)
			{
				++awake;
				++dirtyRecords;
				// Records dirty in appearance alone, such as dormant monsters
				// cycling their spawn animation.
				if (mask == SNAPGRP_APPEAR)
					++animOnly;

				snap_TallyGroups(mask, cur);

				snap_AppendRecord(cur.id, mask, 0, cur);
				g_idleTics[cur.id] = 0;
			}
			else if (g_idleTics[cur.id] < 0xFFFF)
			{
				if (++g_idleTics[cur.id] >= SNAP_SETTLE_TICS)
					++settled;
			}
			else
				++settled;
		}

		// The population census, per capture, which the report divides by
		// captures for a standing population.
		if ((g_popByClass != NULL) && (cur.classId < SNAP_MAXID))
		{
			++g_popByClass[cur.classId];
			++g_popTotal;
			if ((g_idleTics != NULL) && (g_idleTics[cur.id] >= SNAP_SETTLE_TICS))
			{
				++g_popSettledByClass[cur.classId];
				++g_popSettledTotal;
			}
		}

		g_prevState[cur.id] = cur;
	}

	// Leaves, present last frame and absent now, as id-with-mask-0 tombstones.
	unsigned int leaves = 0;
	for (unsigned int id = 0; id < SNAP_MAXID; ++id)
	{
		if (g_prevPresent[id] && (g_curPresent[id] == 0))
		{
			++leaves;
			SnapEntityState dummy;
			snap_AppendRecord((WORD)id, 0, 0, dummy);
			g_idleTics[id] = 0;
		}
	}

	// Swap presence maps.
	{
		BYTE* tmp = g_prevPresent;
		g_prevPresent = g_curPresent;
		g_curPresent = tmp;
	}

	// The world diff compressed, only for the recorder and the wire-cost
	// line, since the transport compresses per client.
	unsigned int zstdSize = 0;
	if ((g_deltaBuf.Size() > 0) && (g_recordFile != NULL))
	{
		const size_t bound = ZSTD_compressBound(g_deltaBuf.Size());
		if (g_zstdBuf.Size() < bound)
			g_zstdBuf.Resize((unsigned int)bound);
		const size_t r = ZSTD_compress(&g_zstdBuf[0], bound, &g_deltaBuf[0], g_deltaBuf.Size(), fua_snap_zstd_level);
		if (ZSTD_isError(r) == 0)
			zstdSize = (unsigned int)r;
	}

	// The game tics this capture spans, more than one in catch-up.
	unsigned int spanTics = 1;
	if (g_lastCaptureTic >= 0 && gametic > g_lastCaptureTic)
		spanTics = gametic - g_lastCaptureTic;
	g_lastCaptureTic = gametic;
	g_stats.simTics += spanTics;

	// Feed the stream recorder, if active.
	snap_RecordCapture(spanTics, g_deltaBuf, zstdSize);

	// Transmit per-client deltas (clienttrack.cpp).
	g_cePumpTics = spanTics;
	snap_CosReleaseDue();
	if (fua_snap_send)
		{
			const tictime_raw_t sendStart = TIC_GetRawCounter();
			snap_SendToClients();
			g_phaseSendRaw += (QWORD)(TIC_GetRawCounter() - sendStart);
		}
	// This pump's cosmetic events have been counted for every client; a
	// release stamped past it waits for the next.
	{
		unsigned int w = 0;
		for (unsigned int r = 0; r < g_ceEvents.Size(); ++r)
			if (g_ceEvents[r].tic > gametic - 1)
				g_ceEvents[w++] = g_ceEvents[r];
		g_ceEvents.Resize(w);
		g_ceRefused.Clear();
	}

	g_stats.frames++;
	g_stats.entities += g_capture.Size();
	g_stats.awake += awake;
	g_stats.settled += settled;
	g_stats.enters += enters;
	g_stats.leaves += leaves;
	g_stats.deltaBytes += g_deltaBuf.Size();
	g_stats.zstdBytes += zstdSize;
	g_stats.dirtyRecords += dirtyRecords;
	g_stats.animOnly += animOnly;
	if (g_deltaBuf.Size() > g_stats.peakDelta) g_stats.peakDelta = g_deltaBuf.Size();
	if (zstdSize > g_stats.peakZstd) g_stats.peakZstd = zstdSize;
	if (g_capture.Size() > g_stats.peakEntities) g_stats.peakEntities = g_capture.Size();
}

//*****************************************************************************
//	TRANSMISSION

static TArray<BYTE>	g_clientDelta;
static QWORD		g_sentBytes = 0;

// Priority accounting over the report window.
static QWORD		g_tierAccum[SNAPCL_TIERS_REPORT];
static QWORD		g_forcedAccum = 0;
static QWORD		g_cosmeticAccum = 0;
static QWORD		g_deferredAccum = 0;
static unsigned int	g_priorityFrames = 0;
static QWORD		g_evSpanTics = 0;		// game tics the event counters cover
static unsigned int	g_sentPackets = 0;

// Wire accounting: the payload against what was sent, whose peak ratio sizes
// the budget's headroom.
static QWORD		g_wirePayloadBytes = 0;		// uncompressed, as handed to the transport
static QWORD		g_wireSentBytes = 0;		// after Huffman/Zstandard
static unsigned int	g_wirePackets = 0;
static unsigned int	g_wireZstdPackets = 0;
static unsigned int	g_wirePeak = 0;
static QWORD		g_freshAccum = 0;
static QWORD		g_resendAccum = 0;
static QWORD		g_resendByteAccum = 0;
static QWORD		g_tierSizeAccum[SNAPCL_TIERS_REPORT];
static QWORD		g_resAllowAccum = 0, g_resSpentAccum = 0;
static QWORD		g_resCappedAccum = 0, g_resDrainedAccum = 0;

// Clients with a snapshot queued in their unreliable buffer this tic, so the
// per-client send rate can be checked against the tic rate.
static bool			g_snapshotQueued[MAXPLAYERS] = { false };


// Everything the server put on the wire, both channels, so the snapshot path
// can be compared against what legacy replication still costs beside it.
static QWORD		g_outboundAtWindowStart = 0;
static bool			g_haveOutboundMark = false;

// The packet room at the configured packet size (ComputePacketRoom).
static unsigned int snap_PacketRoom()
{
	return ComputePacketRoom((unsigned int)SERVER_GetMaxPacketSize());
}

// The frame's budget at the set fua_snap_maxbytes (ComputeEffectiveBudget).
static unsigned int snap_EffectiveBudget()
{
	return ComputeEffectiveBudget(fua_snap_maxbytes);
}

// One datagram's share of the budget at the configured packet size
// (ComputeChunkLimit).
static unsigned int snap_ChunkLimit()
{
	return ComputeChunkLimit((unsigned int)SERVER_GetMaxPacketSize());
}

static TArray<SnapCosPick> g_cePicks;
static TArray<BYTE>	g_ceFates;			// each pick past the fit: SNAP_COSO_*
static TArray<BYTE>	g_ceCmd;			// this client's event bodies, ages in
static unsigned int	g_ceCmdCount = 0;
static bool			g_ceCmdAppend = false;	// fits in the last chunk's datagram

static double snap_CosDist(const SnapCosEvent& ev, const AActor* pView)
{
	if (pView == NULL)
		return 0.0;
	const double dx = FIXED2FLOAT(ev.x - pView->x);
	const double dy = FIXED2FLOAT(ev.y - pView->y);
	const double dz = FIXED2FLOAT(ev.z - pView->z);
	return sqrt(dx * dx + dy * dy + dz * dz);
}

// This pump's events for one client, true when a command is ready (mode 2):
// the cull, the shooter skip, the bank, the datagram cap with last pump's
// overflow in the pick, and the fit in the last chunk's datagram (-1: none).
static bool snap_CosClientPump(unsigned int client, int lastChunkLen)
{
	if ((snap_CosActive() == false) || (client >= MAXPLAYERS))
		return false;
	const unsigned int tics = (g_cePumpTics > 0) ? g_cePumpTics : 1;
	g_ceClientTics += tics;
	++g_ceClientPumps;
	// Per tic simulated, so a catch-up pump is not starved; four tics' worth
	// saved while quiet carries a burst.
	const unsigned int perTic = (unsigned int)MAX<int>(0, fua_snap_cosmetic_bytes);
	g_ceBank[client] = ComputeCosBankRefill(g_ceBank[client], perTic, tics);
	TArray<SnapCosEvent>& carry = g_ceCarry[client];
	if ((g_ceEvents.Size() == 0) && (carry.Size() == 0))
		return false;

	const AActor* pView = (players[client].camera != NULL) ? players[client].camera : players[client].mo;
	const double range = (double)MAX<int>(0, fua_snap_cosmetic_dist);
	g_cePicks.Clear();
	unsigned int wanted = 0, kept = 0;
	for (unsigned int e = 0; e < g_ceEvents.Size(); ++e)
	{
		const SnapCosEvent& ev = g_ceEvents[e];
		if (ev.tic > gametic - 1)
			continue;			// a release, due next pump
		if (ev.shooter == (int)client)
		{
			++g_ceShooterSkips;
			continue;
		}
		const double dist = snap_CosDist(ev, pView);
		if (dist > range)
		{
			++g_ceCulled;
			continue;
		}
		if (ev.kind >= SNAP_COSE_BLOOD)
		{
			++g_ceBloodKept;
			// A client without the originator cannot place the blood.
			if ((ev.originator == 0) || (Believes(client, ev.originator) == false))
				++g_ceBloodUnknown;
		}
		// Age from the pump's newest simulated tic: ServerTick runs after
		// the loop's last gametic++.
		if ((gametic - 1) - ev.tic > 7)
			++g_ceAgeSaturated;
		SnapCosPick pick;
		pick.dist = (unsigned int)dist;
		pick.index = e;
		pick.len = ev.len;
		pick.carried = 0;
		g_cePicks.Push(pick);
		wanted += ev.len;
		++kept;
	}
	g_ceKept += kept;
	// What this pump made for the client; carried events were counted in theirs.
	g_ceWanted += wanted;
	const unsigned int nNew = g_ceEvents.Size();
	for (unsigned int c = 0; c < carry.Size(); ++c)
	{
		const SnapCosEvent& ev = carry[c];
		if ((gametic - 1) - ev.tic > 7)
		{
			++g_ceCarryAged;
			continue;
		}
		SnapCosPick pick;
		pick.dist = (unsigned int)snap_CosDist(ev, pView);
		pick.index = nNew + c;
		pick.len = ev.len;
		pick.carried = 1;
		g_cePicks.Push(pick);
		wanted += ev.len;
	}

	const unsigned int room = snap_PacketRoom() - SNAP_COSEVENT_HEADER;
	const unsigned int cap = MIN<unsigned int>(g_ceBank[client], room);
	unsigned int keep = g_cePicks.Size();
	unsigned int sent = wanted;
	g_ceCarryNext.Clear();
	if (wanted > cap)
	{
		++g_cePumpsCapped;
		g_ceBankAtCap += g_ceBank[client];
		qsort(&g_cePicks[0], g_cePicks.Size(), sizeof(SnapCosPick), ComputeCosPickCmp);
		keep = ComputeCosFit(&g_cePicks[0], g_cePicks.Size(), cap, sent);
		// The overflow: a carried event does not get a second pump.
		g_ceFates.Resize(g_cePicks.Size());
		ComputeCosOverflow(&g_cePicks[0], g_cePicks.Size(), keep, room, &g_ceFates[0]);
		for (unsigned int k = keep; k < g_cePicks.Size(); ++k)
		{
			if (g_ceFates[k] == SNAP_COSO_CARRIED_AGAIN)
				++g_ceCarryOver;
			else if (g_ceFates[k] == SNAP_COSO_NO_ROOM)
				++g_ceCapDropped;
			else
			{
				g_ceCarryNext.Push(g_ceEvents[g_cePicks[k].index]);
				++g_ceCarried;
			}
		}
	}
	g_ceBank[client] -= MIN<unsigned int>(sent, g_ceBank[client]);
	g_ceSent += sent;

	bool bReady = false;
	if (sent > 0)
	{
		g_ceCmdAppend = ComputeCosAppends(lastChunkLen, sent, (int)SERVER_GetMaxPacketSize(),
			SNAP_PACKET_OVERHEAD, SNAP_COSEVENT_HEADER);
		if (g_ceCmdAppend)
			++g_ceAppended;
		else
			++g_ceAlone;
		if (snap_CosActs())
		{
			g_ceCmd.Resize(sent);
			BYTE* p = &g_ceCmd[0];
			for (unsigned int k = 0; k < keep; ++k)
			{
				const SnapCosPick& pick = g_cePicks[k];
				const SnapCosEvent& ev = pick.carried ? carry[pick.index - nNew] : g_ceEvents[pick.index];
				memcpy(p, ev.data, ev.len);
				p[0] |= (BYTE)(clamp<int>((gametic - 1) - ev.tic, 0, 7) << SNAP_COSE_AGE_SHIFT);
				p += ev.len;
			}
			g_ceCmdCount = keep;
			bReady = true;
		}
	}
	carry = g_ceCarryNext;
	return bReady;
}

// The command snap_CosClientPump prepared; flushed when it goes alone.
static void snap_CosSendEvents(unsigned int client, bool bFlush)
{
	NetCommand command(SVC2_SNAPCOSMETIC);
	command.addByte(g_snapMapStamp);
	command.addShort((int)g_ceCmdCount);
	command.addBuffer(&g_ceCmd[0], g_ceCmd.Size());
	command.setUnreliable(true);
	command.sendCommandToOneClient(client);
	if (bFlush)
		SERVER_SendClientPacket(client, false);
}

static void snap_SendToClients()
{
	++g_snapFrame;

	const unsigned int budget = snap_EffectiveBudget();

	// The presence dump, once every fua_snap_debug_presencelog seconds.
	static unsigned int s_presenceDueMS = 0;
	const unsigned int presenceNowMS = I_MSTime();
	const bool bPresenceDue = (fua_snap_debug_presencelog > 0)
		&& ((int)(presenceNowMS - s_presenceDueMS) >= 0);
	if (bPresenceDue)
		s_presenceDueMS = presenceNowMS + (unsigned int)fua_snap_debug_presencelog * 1000u;

	// An empty world is legal and still builds a frame, its entities all
	// leaving, so element zero of an empty TArray is not taken.
	const SnapEntityState* pWorld = (g_capture.Size() > 0) ? &g_capture[0] : NULL;
	const BYTE* pPriority = ((g_capturePriority.Size() == g_capture.Size()) &&
		(g_capturePriority.Size() > 0)) ? &g_capturePriority[0] : NULL;

	for (unsigned int i = 0; i < MAXPLAYERS; ++i)
	{
		if (snap_ClientInMap(i) == false)
			continue;

		// Where this client is looking, so near-and-in-front wins the budget.
		SnapViewpoint view;
		view.valid = false;
		view.x = view.y = view.z = 0;
		view.angle = 0;
		{
			const AActor* pCamera = (players[i].camera != NULL) ? players[i].camera : players[i].mo;
			if (pCamera != NULL)
			{
				view.valid = true;
				view.x = pCamera->x;
				view.y = pCamera->y;
				view.z = pCamera->z;
				view.angle = pCamera->angle;
			}
		}

		// Sequences are allocated by the builder, one per chunk, and only when a
		// payload is actually produced.
		const unsigned int n = BuildDelta(i, pWorld, pPriority,
			g_capture.Size(), g_snapFrame, (unsigned int)gametic,
			g_chunks, g_clientDelta, budget,
			snap_ChunkLimit(), view);
		if (bPresenceDue)
			PresenceLog(i, g_snapFrame);
		// Cosmetic events: worked out before a client with no chunk is
		// skipped, as its events then go alone.
		const bool bCosEvents = snap_CosClientPump(i, ((n > 0) && (g_chunks.Size() > 0))
			? (int)g_chunks[g_chunks.Size() - 1].length : -1);
		bool bCosSent = false;
		if ((n == 0) || (g_chunks.Size() == 0))
		{
			if (bCosEvents)
				snap_CosSendEvents(i, true);
			continue;
		}

		// One unreliable datagram per chunk, the loss injector rolling for
		// each, and a dropped one counted as if it left (sv_snapshot.md).
		for (unsigned int c = 0; c < g_chunks.Size(); ++c)
		{
			const SnapChunk& ch = g_chunks[c];
			snap_ReleaseHeldDatagrams(i, ch.seq);
			if ((fua_snap_debug_loss > 0) && ((rand() % 100) < fua_snap_debug_loss))
			{
				++g_droppedPackets;
				continue;
			}

			SnapDatagramHeader h;
			// The frame counts pumps, so elapsed time rides as the absolute
			// game tic, which a lost frame cannot desync as a span would.
			h.frame = (SDWORD)g_snapFrame;
			h.gameTic = (SDWORD)gametic;
			h.seq = (SDWORD)ch.seq;
			h.mapStamp = g_snapMapStamp;
			h.resetCount = g_snapResetCount;
			// How far behind this datagram the newest ack processed is, so the
			// client counts a checksum only once every loss before it was repaired.
			h.ackLag = AckLag(i, ch.seq);
			// The newest reliable packet sent, low 16 bits: the client requests
			// a lost tail from it instead of waiting for later reliable traffic.
			h.reliable = (WORD)(SERVER_GetClient(i)->SavedPackets.NextSequence() - 1);

			// The hold-back reproducer keeps the datagram as built and sends it
			// later; its cosmetic events go alone below.
			if ((fua_snap_debug_holdback > 0) && ((rand() % 100) < fua_snap_debug_holdback)
				&& (g_heldDatagrams[i].Size() < SNAP_HOLDBACK_MAX)
				&& (ch.length <= SNAP_HOLDBACK_SLOT))
			{
				if (g_heldBytes[i] == NULL)
					g_heldBytes[i] = new BYTE[SNAP_HOLDBACK_MAX * SNAP_HOLDBACK_SLOT];
				unsigned int slot = 0;
				while (snap_HeldSlotUsed(i, slot))
					++slot;
				SnapHeldDatagram held;
				held.header = h;
				held.releaseSeq = ch.seq + (unsigned int)clamp<int>(fua_snap_debug_holdbackgap, 1, 64);
				held.slot = slot;
				held.length = ch.length;
				memcpy(g_heldBytes[i] + slot * SNAP_HOLDBACK_SLOT, &g_clientDelta[ch.offset],
					ch.length);
				g_heldDatagrams[i].Push(held);
				++g_heldPackets;
				continue;
			}

			snap_SendDatagram(i, h, &g_clientDelta[ch.offset], ch.length);
			// The cosmetic events share the last chunk's datagram when they fit.
			if (bCosEvents && g_ceCmdAppend && (c + 1 == g_chunks.Size()))
			{
				snap_CosSendEvents(i, false);
				bCosSent = true;
			}
			// Every chunk is flushed, or it would share a datagram with the
			// unreliable traffic after it.
			SERVER_SendClientPacket(i, false);
			++g_chunkDatagrams;
		}
		if (bCosEvents && (bCosSent == false))
			snap_CosSendEvents(i, true);
		g_chunkFrames += (g_chunks.Size() > 0) ? 1 : 0;

		g_sentBytes += n;
		++g_sentPackets;
		g_freshAccum += LastFresh();
		g_resendAccum += LastResend();
		g_resendByteAccum += LastResendBytes();

		// SNAPCL_TIERS_REPORT, not a literal, or the last tiers read as
		// bands that never send.
		for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
		{
			g_tierAccum[t] += LastTierCount(t);
			g_tierSizeAccum[t] += LastTierSize(t);
		}
		g_resAllowAccum += LastReserveAllowed();
		g_resSpentAccum += LastReserveSpent();
		g_resCappedAccum += LastReserveCapped();
		g_resDrainedAccum += LastReserveDrained();
		g_forcedAccum += LastForced();
		g_cosmeticAccum += LastCosmetic();
		g_deferredAccum += LastDeferred();
		++g_priorityFrames;
	}
}

void zx::netsnapshot::ClientAcknowledged(unsigned int client, unsigned int seq, unsigned int history)
{
	if ((fua_snap_debug_ackdelay > 0) && (client < MAXPLAYERS))
	{
		SnapDelayedAck held;
		held.seq = seq;
		held.history = history;
		// The delay is measured in FRAMES, so it stays on the frame counter.
		held.dueFrame = g_snapFrame + (unsigned int)fua_snap_debug_ackdelay;
		g_delayedAcks[client].Push(held);
		return;
	}

	OnAck(client, seq, history);
}

void zx::netsnapshot::NoteSnapshotQueued(unsigned int client)
{
	if (client < MAXPLAYERS)
		g_snapshotQueued[client] = true;
}

bool zx::netsnapshot::ConsumeSnapshotQueued(unsigned int client)
{
	if (client >= MAXPLAYERS)
		return false;
	const bool had = g_snapshotQueued[client];
	g_snapshotQueued[client] = false;
	return had;
}

void zx::netsnapshot::NoteUnreliablePacket(unsigned int payloadBytes, unsigned int wireBytes, bool usedZstd)
{
	if ((snap_GateOn() == false) || (payloadBytes == 0))
		return;

	g_wirePayloadBytes += payloadBytes;
	g_wireSentBytes += wireBytes;
	++g_wirePackets;
	if (usedZstd)
		++g_wireZstdPackets;
	if (wireBytes > g_wirePeak)
		g_wirePeak = wireBytes;
}

// CVAR_SERVERINFO, but it syncs to RCON admins only, not to gameplay
// clients, so client-side code must not gate on it.
CVAR(Int, fua_snap_nolegacy, 0, CVAR_NOSETBYACS | CVAR_SERVERINFO)




bool zx::netsnapshot::SuppressesLegacy( const AActor *pActor, int updateClass )
{
	// Nothing is suppressed unless deltas are going out, since capturing
	// alone leaves the legacy channel the only one.
	if ((snap_GateOn() == false) || (fua_snap_send == false))
		return false;

	// A routed cosmetic reaches clients as an event, at any level; a legacy
	// command for it would duplicate the client's copy.
	if ((pActor != NULL) && (pActor->NetID == 0) && CosmeticRouted(pActor))
		return true;

	if (fua_snap_nolegacy < updateClass)
		return false;

	if (pActor == NULL)
		return false;

	// No net id means the capture sweep never sees it, so no snapshot record
	// will ever replace what we would be suppressing here.
	if (pActor->NetID == 0)
		return false;

	// A player is local to one client and remote to the rest, which
	// SuppressesLegacyPlayer answers per client.
	if (pActor->player != NULL)
		return false;

	return true;
}

// Whether a client's periodic MovePlayer for other players is suppressed (level
// 3), counted, the entity record, psprite stream and crouch bits replacing it.

// Client-frames whose legacy MovePlayer sweep was skipped this window.
static QWORD g_legacyPlayerSkips = 0;

bool zx::netsnapshot::SuppressesLegacyPlayer(unsigned int client)
{
	if ((snap_GateOn() == false) || (fua_snap_send == false))
		return false;
	if (fua_snap_nolegacy < 3)
		return false;
	if (client >= MAXPLAYERS)
		return false;
	++g_legacyPlayerSkips;
	return true;
}

QWORD zx::netsnapshot::LegacyPlayerSkips(bool bReset)
{
	const QWORD n = g_legacyPlayerSkips;
	if (bReset)
		g_legacyPlayerSkips = 0;
	return n;
}

// Whether a client's own MoveLocalPlayer and SetLocalPlayerJumpTics give way to
// the local block (level 3), both, since they write one prediction cursor.

bool zx::netsnapshot::SuppressesLegacyLocal(unsigned int client)
{
	if ((snap_GateOn() == false) || (fua_snap_send == false))
		return false;
	if (fua_snap_nolegacy < 3)
		return false;
	if (client >= MAXPLAYERS)
		return false;
	// A spectator is never sent a block, so nothing replaces its commands.
	if (players[client].bSpectating)
		return false;
	return true;
}

// Each client's last sound-table answer on its ack (SNAP_SOUNDIDS_* bits).
static BYTE g_soundIdsAnswer[MAXPLAYERS];

void zx::netsnapshot::ClientConnected(unsigned int client)
{
	ServerInit();
	ResetClient(client);
	snap_CosClientReset(client);
	if (snap_CosActive())
	{
		const unsigned int n = (g_cosMarks.CountUsed() > 0) ? snap_CosCountAlive(NULL, NULL, NULL) : 0;
		++g_ceJoins;
		g_ceJoinAlive += n;
		if (n > g_ceJoinWorst)
			g_ceJoinWorst = n;
	}
	if (client < MAXPLAYERS)
	{
		g_delayedAcks[client].Clear();
		g_heldDatagrams[client].Clear();
		g_soundIdsAnswer[client] = 0;	// a previous occupant's answer is not this client's
	}
}

void zx::netsnapshot::ClientDisconnected(unsigned int client)
{
	ServerInit();
	ResetClient(client);
	snap_CosClientReset(client);
	if (client < MAXPLAYERS)
	{
		g_delayedAcks[client].Clear();
		g_heldDatagrams[client].Clear();
		g_soundIdsAnswer[client] = 0;
	}
}

// Snapshot sounds for entities with a mirror, which runs no action functions,
// culled per client against the engine's rolloff.
CVAR(Bool, fua_snap_soundevents, true, CVAR_ARCHIVE | CVAR_NOSETBYACS)

static unsigned int g_soundSent = 0, g_soundCulled = 0, g_soundPositional = 0;
static unsigned int g_soundRoutedPoint = 0;	// NetID-less routed cosmetics, as points

// Sounds by table index rather than name, to a client whose first N names match
// the server's (a pack loaded before the server's files shifts them).
CVAR(Bool, fua_snap_soundids, true, CVAR_NOSETBYACS)

static unsigned int	g_soundById = 0, g_soundByName = 0;
// Why a snapshot sound went by name, and sounds never sent at all.
static unsigned int	g_soundNameRange = 0, g_soundNameChannel = 0;
static unsigned int	g_soundNameOff = 0, g_soundNameUnmatched = 0;
static unsigned int	g_soundUnplayable = 0;

bool zx::netsnapshot::SoundUnplayable(int soundId, float volume)
{
	if ((soundId > 0) && (volume > 0.f))
		return false;
	++g_soundUnplayable;
	return true;
}

// This map's table, after its SNDINFO rebuild; ids past its count go by name.
static unsigned int	g_soundTableCount = 0;
static unsigned int	g_soundTableLo = 0, g_soundTableHi = 0;

void zx::netsnapshot::SoundTableHash(unsigned int count, unsigned int& lo, unsigned int& hi)
{
	// FNV-1a 64 over each lowercased name and its terminator.
	QWORD h = 14695981039346656037ull;
	const unsigned int n = (count < S_sfx.Size()) ? count : S_sfx.Size();
	for (unsigned int i = 0; i < n; ++i)
	{
		const char *p = S_sfx[i].name.GetChars();
		do
		{
			h ^= (BYTE)tolower((BYTE)*p);
			h *= 1099511628211ull;
		} while (*p++ != '\0');
	}
	lo = (unsigned int)h;
	hi = (unsigned int)(h >> 32);
}

void zx::netsnapshot::TakeSoundTable()
{
	if (NETWORK_GetState() != NETSTATE_SERVER)
		return;
	// An id travels as a Short; a larger table takes 0, which never matches.
	g_soundTableCount = (S_sfx.Size() < 65536) ? S_sfx.Size() : 0;
	SoundTableHash(g_soundTableCount, g_soundTableLo, g_soundTableHi);
}

void zx::netsnapshot::SendSoundTable(unsigned int client)
{
	// A gate-off server sends the legacy protocol and nothing else.
	if (snap_GateOn() == false)
		return;
	NetCommand command(SVC2_SNAPSOUNDTABLE);
	command.addByte(g_snapMapStamp);
	command.addShort((int)g_soundTableCount);
	command.addLong((SDWORD)g_soundTableLo);
	command.addLong((SDWORD)g_soundTableHi);
	command.sendCommandToOneClient(client);
}

void zx::netsnapshot::ClientSoundIds(unsigned int client, BYTE answer)
{
	if (client < MAXPLAYERS)
		g_soundIdsAnswer[client] = answer;
}

// Whether this client matched this map's table, names until it answers.
static bool snap_ClientTakesSoundIds(unsigned int client)
{
	const BYTE a = g_soundIdsAnswer[client];
	return fua_snap_soundids && ((a & SNAP_SOUNDIDS_MATCH) != 0)
		&& ((a & SNAP_SOUNDIDS_STAMP) == (g_snapMapStamp & SNAP_SOUNDIDS_STAMP));
}

// Sends SoundActor's or SoundPoint's values with the index for the name, volume
// and attenuation omitted at their defaults.
static void snap_SendSoundId(unsigned int client, const AActor *pActor, bool bPoint,
	int channel, int soundId, float volume, float attenuation)
{
	const int vol = (int)(clamp(volume, 0.0f, 2.0f) * 127);
	const int atten = NETWORK_AttenuationFloatToInt(attenuation);
	int flags = bPoint ? SNAP_SOUND_POINT : 0;
	// A point drops the loop, as SoundPoint's Byte channel does: a loop with
	// no actor could never be stopped.
	if ((bPoint == false) && ((channel & CHAN_LOOP) != 0))
		flags |= SNAP_SOUND_LOOP;
	if (vol != 127)
		flags |= SNAP_SOUND_VOLUME;
	if (atten != ATTN_INT_NORM)
		flags |= SNAP_SOUND_ATTEN;

	NetCommand command(SVC2_SNAPSOUND);
	command.addByte(flags);
	if (bPoint)
	{
		// Floored, as the cosmetic events are, and relative to the frame in its
		// width, byte by byte, so a narrow axis is the SHORT it always was.
		const SnapFrame &frame = ServerMapFrame();
		const fixed_t pos[3] = { pActor->x, pActor->y, pActor->z };
		for (unsigned int k = 0; k < 3; ++k)
		{
			BYTE rel[SNAP_FRAME_WIDEST];
			const unsigned int n = PutPosIn(rel, frame, k, FloorPosIn(frame, k, pos[k]));
			for (unsigned int b = 0; b < n; ++b)
				command.addByte(rel[b]);
		}
	}
	// A point carries it too: the client moves the sound onto the mirror once
	// one exists, so it travels with the entity as stock's does.
	command.addShort(pActor->NetID);
	command.addByte(channel & 0xFF);
	command.addShort(soundId);
	if (flags & SNAP_SOUND_VOLUME)
		command.addByte(vol);
	if (flags & SNAP_SOUND_ATTEN)
		command.addByte(atten);
	if (pActor->NetID == 0)
		command.setUnreliable(true);
	command.sendCommandToOneClient(client);
}

// Check if the client is in the server and send the records it needs.
static bool snap_ClientInMap(unsigned int client)
 {
	if (SERVER_IsValidClient(client) == false)
		return false;

	const CLIENT_s * pClient = SERVER_GetClient(client);
	return (pClient != NULL) && (pClient->State == CLS_SPAWNED);
}

bool zx::netsnapshot::BroadcastActorSound(AActor *pActor, int channel, int soundId,
	float volume, float attenuation, bool bSoundOnClient)
{
	if ((fua_snap_soundevents == false) || (pActor == NULL))
		return false;

	// With no NetID, a routed copy's own action sound is its client copy's,
	// and anything else on it goes as a culled point.
	const bool bNoNetID = (pActor->NetID == 0);
	if (bNoNetID)
	{
		if (CosmeticMutesSound(pActor))
			return true;
		if (CosmeticRouted(pActor) == false)
			return false;
		if (bSoundOnClient == false)
			return true;			// stock tells no client
		++g_soundRoutedPoint;
	}
	// Only for entities the client cannot hear about any other way.
	else if (SuppressesLegacy(pActor, SNAPLEGACY_EXISTENCE) == false)
		return false;

	const char *pszName = S_GetName(FSoundID(soundId));
	if (pszName == NULL)
		return true;				// nothing sensible to send; do not fall through

	// By id only below the hashed count, and when the channel fits the low
	// byte plus CHAN_LOOP; anything else goes by name as before.
	const bool bIdInRange = (soundId > 0) && ((unsigned int)soundId < g_soundTableCount);
	const bool bChannelFits = ((channel & ~(0xFF | CHAN_LOOP)) == 0);
	const bool bIdFits = bIdInRange && bChannelFits;

	for (unsigned int i = 0; i < MAXPLAYERS; ++i)
	{
		if (snap_ClientInMap(i) == false)
			continue;

		const AActor *pListener = (players[i].camera != NULL) ? players[i].camera : players[i].mo;
		if (pListener == NULL)
			continue;

		const double dx = (double)(pActor->x - pListener->x) / 65536.0;
		const double dy = (double)(pActor->y - pListener->y) / 65536.0;
		const double dz = (double)(pActor->z - pListener->z) / 65536.0;
		const float dist = (float)sqrt(dx * dx + dy * dy + dz * dz);

		if (S_IsAudibleAtDistance(FSoundID(soundId), attenuation, dist) == false)
		{
			++g_soundCulled;
			continue;
		}

		++g_soundSent;
		const bool bById = bIdFits && snap_ClientTakesSoundIds(i);
		if (bById)
			++g_soundById;
		else
		{
			++g_soundByName;
			if (bIdInRange == false)
				++g_soundNameRange;
			else if (bChannelFits == false)
				++g_soundNameChannel;
			else if (fua_snap_soundids == false)
				++g_soundNameOff;
			else
				++g_soundNameUnmatched;
		}

		// Positional until the client has believed in the entity for
		// fua_snap_sound_entergrace frames (g_snapFrame, as `enterFrame` is).
		const unsigned int believedFor = bNoNetID ? 0 : BelievedFrames(i, pActor->NetID, g_snapFrame);
		if ((bNoNetID == false) && Believes(i, pActor->NetID)
			&& (believedFor >= (unsigned int)fua_snap_sound_entergrace))
		{
			if (bById)
				snap_SendSoundId(i, pActor, false, channel, soundId, volume, attenuation);
			else
				SERVERCOMMANDS_SoundActor(pActor, channel, pszName, volume, attenuation,
					i, SVCF_ONLYTHISCLIENT);
		}
		else
		{
			++g_soundPositional;
			if (bById)
				snap_SendSoundId(i, pActor, true, channel, soundId, volume, attenuation);
			else
				// [Kaori-Chani] The stock command's coordinates are LONG, so a 48.16 position is
				// cast the way every other caller in the engine casts it ((LONG)self->x in
				// a_bossbrain.cpp, p_mobj.cpp, p_switch.cpp): the raw, truncated to 32 bits. That
				// keeps this call the same shape as the engine's own and leaves the stock sound
				// command's width ForkUnderA's decision rather than ours, as the snapshot's own
				// wire stays 32-bit on purpose too.
				SERVERCOMMANDS_SoundPoint((LONG)pActor->x, (LONG)pActor->y, (LONG)pActor->z,
					channel, pszName, volume, attenuation, i, SVCF_ONLYTHISCLIENT);
		}
	}
	return true;
}

void zx::netsnapshot::MapChanged()
{
	// The level teardown runs everywhere, but this is the server's alone.
	if ( NETWORK_GetState() != NETSTATE_SERVER )
		 return;
	
	// The next map reuses ids without leaves and may take another frame, so a
	// recording holds one map and ends here.
	snap_RecordStop("map changed");

	// [Kaori-Chani] And an fua_snap set during the last map takes effect here.
	snap_GateLatch();

	++g_snapMapStamp;
	g_mapFrameDue = true;
	// An action that threw would have left its actor here; it is not valid now.
	g_pSnapActionSelf = NULL;
	g_pSnapTickSelf = NULL;
	snap_CosEventsReset();
	// A pending one belongs to the map being torn down; the next map's joiners
	// take the count as it stands.
	g_resetSplitDue = 0;
	g_snapResetCountSent = g_snapResetCount;

	// Re-assert the flags: the cvar may be set from the config before actor
	// classes are registered, in which case its callback found nothing.
	snap_ApplyNoNetIDClasses(fua_snap_nonetid_classes);

	// The branch detector's FState* per NetID, which reused ids would
	// read as branches (9.2).
	BranchReset();
	StraightReset();

	// Same reason: mover slot identity is a thinker pointer, and the teardown
	// frees every thinker on the map.
	MoverReset();

	// Per-client believed sets first, every id in them about to be reused.
	ServerInit();
	ResetAllClients();
	for (unsigned int i = 0; i < MAXPLAYERS; ++i)
	{
		g_delayedAcks[i].Clear();
		g_heldDatagrams[i].Clear();
	}

	// Then the capture side, or an id live on both maps is a dirty record
	// with no classId.
	if (g_snapInitialized)
	{
		memset(g_prevPresent, 0, SNAP_MAXID);
		memset(g_curPresent, 0, SNAP_MAXID);
		memset(g_idleTics, 0, SNAP_MAXID * sizeof(WORD));

	}

	// gametic restarts with the level, so the span since the last capture is
	// not meaningful across the boundary.
	g_lastCaptureTic = -1;
}

//*****************************************************************************
//	REPORTING

static void snap_MaybeReport()
{
	if ((fua_snap_stats <= 0) || (g_stats.frames == 0))
		return;

	// A threshold on elapsed tics, not a modulo, which catch-up would skip.
	if (gametic - g_lastReportTic < fua_snap_stats * TICRATE)
		return;

	// A report due while the last still drains is skipped and counted.
	if (g_reportQueue.Size() > 0)
	{
		++g_reportSkipped;
		return;
	}
	g_lastReportTic = gametic;

	const double f = (double)g_stats.frames;
	const double simRatio = (double)g_stats.simTics / f;	// 1.00 = holding 35 Hz
	// Rates are per second of game time, not per pump.
	const double gameSec = (double)g_stats.simTics / (double)TICRATE;
	const double pumpsPerSec = f / gameSec;
	// The event counters clear only when the branch block prints, so they can
	// span several windows; their game time and pumps are kept alongside.
	g_evSpanTics += g_stats.simTics;
	const double avgDelta = (double)g_stats.deltaBytes / f;
	// Zero whenever the compression above was gated off, so it is meaningful
	// only under the condition the print below branches on.
	const double avgZstd = (double)g_stats.zstdBytes / f;
	const double avgDirty = (double)g_stats.dirtyRecords / f;
	const double bytesPerDirty = (g_stats.dirtyRecords > 0)
		? (double)g_stats.deltaBytes / (double)g_stats.dirtyRecords : 0.0;
	const unsigned int budget = snap_EffectiveBudget();
	const int entsPerBudget = (bytesPerDirty > 0.0)
		? (int)((double)budget / bytesPerDirty) : 0;

	const double animPct = (g_stats.dirtyRecords > 0)
		? 100.0 * (double)g_stats.animOnly / (double)g_stats.dirtyRecords : 0.0;


	snap_Say("[snap] sim=%.2f t/capture%s | ents=%d (peak %u) awake=%d settled=%d anim-only=%.1f/frame (%.0f%% of dirty) enter=%.1f/frame leave=%.1f/frame"
		" | owned items not captured %.0f\n",
		simRatio, (simRatio > 1.05) ? " (SATURATED)" : "",
		(int)(g_stats.entities / g_stats.frames), g_stats.peakEntities,
		(int)(g_stats.awake / g_stats.frames), (int)(g_stats.settled / g_stats.frames),
		(double)g_stats.animOnly / f, animPct,
		(double)g_stats.enters / f, (double)g_stats.leaves / f,
		(g_huntingFrames != 0) ? (double)g_ownedAccum / (double)g_huntingFrames : 0.0);

	// Live MF_COUNTKILL actors holding a target: are the monsters awake.
	if (g_huntingFrames != 0)
	{
		snap_Say("[snap] monsters: %.0f live, %.0f hunting (%.0f%%)\n",
			(double)(QWORD)g_liveAccum / (double)g_huntingFrames,
			(double)(QWORD)g_huntingAccum / (double)g_huntingFrames,
			(g_liveAccum > 0)
				? (100.0 * (double)(QWORD)g_huntingAccum / (double)(QWORD)g_liveAccum) : 0.0);
	}
	// The top few entering classes, largest first, by selection sort.
	if (g_enterClassTotal > 0)
	{
		char szEnter[512];
		size_t at = 0;
		unsigned int shown = 0, named = 0;
		unsigned int pick[SNAP_ENTERCLASS_REPORT] = { 0 };

		for (unsigned int n = 0; n < SNAP_ENTERCLASS_REPORT; ++n)
		{
			unsigned int best = 0, bestId = 0;
			for (unsigned int c = 0; c < SNAP_MAXID; ++c)
			{
				if (g_enterByClass[c] <= best)
					continue;
				bool already = false;
				for (unsigned int k = 0; k < named; ++k)
					if (pick[k] == c) { already = true; break; }
				if (already == false) { best = g_enterByClass[c]; bestId = c; }
			}
			if (best == 0)
				break;
			pick[named++] = bestId;

			const PClass* pType = NETWORK_GetClassFromIdentification((unsigned short)bestId);
			const char* pszName = (pType != NULL) ? pType->TypeName.GetChars() : "?";
			const int wrote = snprintf(szEnter + at, sizeof(szEnter) - at, "%s%s %.1f/frame (%.0f%%)",
				(n > 0) ? ", " : "", pszName,
				(double)best / (double)g_stats.frames,
				100.0 * (double)best / (double)g_enterClassTotal);
			if ((wrote <= 0) || ((size_t)wrote >= sizeof(szEnter) - at))
				break;
			at += (size_t)wrote;
			shown += best;
		}
		snap_Say("[snap] enter classes: %s | other %.0f%% of %u enters\n", szEnter,
			100.0 * (double)(g_enterClassTotal - shown) / (double)g_enterClassTotal,
			g_enterClassTotal);
	}

	// Standing population by class over captures, with the settled share.
	if ((g_popTotal > 0) && (g_stats.frames > 0) && (g_popByClass != NULL))
	{
		char szPop[512];
		size_t at = 0;
		unsigned int named = 0;
		unsigned int pick[SNAP_ENTERCLASS_REPORT] = { 0 };
		
			for (unsigned int n = 0; n < SNAP_ENTERCLASS_REPORT; ++n)
			{
				unsigned int best = 0, bestId = 0;
				for (unsigned int c = 0; c < SNAP_MAXID; ++c)
				{
					if (g_popByClass[c] <= best)
						continue;
					bool already = false;
					for (unsigned int k = 0; k < named; ++k)
						if (pick[k] == c) { already = true; break; }
					if (already == false) { best = g_popByClass[c]; bestId = c; }
				}
				if (best == 0)
					break;
				pick[named++] = bestId;

				const PClass * pType = NETWORK_GetClassFromIdentification((unsigned short)bestId);
				const char* pszName = (pType != NULL) ? pType->TypeName.GetChars() : "?";
				const double pop = (double)best / (double)g_stats.frames;
				const double set = (double)g_popSettledByClass[bestId] / (double)g_stats.frames;
				const int wrote = snprintf(szPop + at, sizeof(szPop) - at,
				"%s%s %.0f (%.0f%% settled)", (n > 0) ? ", " : "", pszName, pop,
				(pop > 0.0) ? (100.0 * set / pop) : 0.0);
				if ((wrote <= 0) || ((size_t)wrote >= sizeof(szPop) - at))
					break;
				at += (size_t)wrote;
			}
		snap_Say("[snap] population: %s | %.0f live, %.0f%% settled\n", szPop,
			(double)g_popTotal / (double)g_stats.frames,
			100.0 * (double)g_popSettledTotal / (double)g_popTotal);
	}

	// Cosmetic classes still holding NetIDs, and those a cosmetic's spawns
	// took.
	if ((g_stats.frames > 0) && (g_enterByClass != NULL) && (g_popByClass != NULL))
	{
		snap_CosEnsure();
		if ((g_cosPrinted == false) || (g_cosPrintedStamp != g_snapMapStamp))
		{
			g_cosPrinted = true;
			g_cosPrintedStamp = g_snapMapStamp;
			unsigned int nRow[SNAPCOS_ROWS] = { 0 };
			for (unsigned int c = 1; c < SNAP_MAXID; ++c)
				if (g_cosVerdict[c] != SNAPCOS_UNKNOWN)
					++nRow[snap_CosRow(c)];
			snap_Say("[snap] cosmetic classes: %u no server copy, %u server copy kept, %u inert, %u plain copy,"
				" %u networked, classified in %.1f ms (fua_snap_cosmetics lists them)\n", nRow[SNAPCOS_ROW_GONE],
				nRow[SNAPCOS_ROW_KEPT], nRow[SNAPCOS_ROW_INERT], nRow[SNAPCOS_ROW_PLAIN], nRow[SNAPCOS_ROW_NONE],
				g_cosClassifyMS);
		}

		unsigned int cosEnters = 0, cosLive = 0;
		unsigned int topId[3] = { 0, 0, 0 }, topN[3] = { 0, 0, 0 };
		for (unsigned int c = 0; c < SNAP_MAXID; ++c)
		{
			if (snap_CosIsCosmetic(c) == false)
				continue;
			cosEnters += g_enterByClass[c];
			cosLive += g_popByClass[c];
			const unsigned int n = g_enterByClass[c];
			for (unsigned int k = 0; k < 3; ++k)
			{
				if (n <= topN[k])
					continue;
				for (unsigned int m = 2; m > k; --m) { topN[m] = topN[m - 1]; topId[m] = topId[m - 1]; }
				topN[k] = n;
				topId[k] = c;
				break;
			}
		}
		char szTop[256];
		size_t at = 0;
		szTop[0] = '\0';
		for (unsigned int k = 0; (k < 3) && (topN[k] > 0); ++k)
		{
			const PClass* pType = NETWORK_GetClassFromIdentification((unsigned short)topId[k]);
			const int wrote = mysnprintf(szTop + at, sizeof(szTop) - at, "%s%s %.1f/frame",
				(k > 0) ? ", " : "", (pType != NULL) ? pType->TypeName.GetChars() : "?",
				(double)topN[k] / (double)g_stats.frames);
			if ((wrote <= 0) || ((size_t)wrote >= sizeof(szTop) - at))
				break;
			at += (size_t)wrote;
		}

		// Networked children of a cosmetic spawner, top three by class.
		unsigned int inhId[3] = { 0, 0, 0 }, inhN[3] = { 0, 0, 0 };
		if (g_cosInheritByClass != NULL)
			for (unsigned int c = 0; c < SNAP_MAXID; ++c)
			{
				const unsigned int n = g_cosInheritByClass[c];
				for (unsigned int k = 0; k < 3; ++k)
				{
					if (n <= inhN[k])
						continue;
					for (unsigned int m = 2; m > k; --m) { inhN[m] = inhN[m - 1]; inhId[m] = inhId[m - 1]; }
					inhN[k] = n;
					inhId[k] = c;
					break;
				}
			}
		char szInh[256];
		size_t inhAt = 0;
		szInh[0] = '\0';
		for (unsigned int k = 0; (k < 3) && (inhN[k] > 0); ++k)
		{
			const PClass* pType = NETWORK_GetClassFromIdentification((unsigned short)inhId[k]);
			const int wrote = mysnprintf(szInh + inhAt, sizeof(szInh) - inhAt, "%s%s %u",
				(k > 0) ? ", " : "", (pType != NULL) ? pType->TypeName.GetChars() : "?", inhN[k]);
			if ((wrote <= 0) || ((size_t)wrote >= sizeof(szInh) - inhAt))
				break;
			inhAt += (size_t)wrote;
		}

		snap_Say("[snap] cosmetic share: enters %.1f/frame (%.0f%% of enters), live %.0f (%.0f%%)%s%s"
			" | networked children of a cosmetic %.1f/frame%s%s (plus %.1f/frame already cosmetic)\n",
			(double)cosEnters / (double)g_stats.frames,
			(g_enterClassTotal > 0) ? (100.0 * (double)cosEnters / (double)g_enterClassTotal) : 0.0,
			(double)cosLive / (double)g_stats.frames,
			(g_popTotal > 0) ? (100.0 * (double)cosLive / (double)g_popTotal) : 0.0,
			(at > 0) ? " | top: " : "", szTop,
			(double)g_cosInheritTotal / (double)g_stats.frames,
			(inhAt > 0) ? ": " : "", szInh,
			(double)g_cosInheritOverlap / (double)g_stats.frames);
		if (g_cosInheritByClass != NULL)
			memset(g_cosInheritByClass, 0, SNAP_MAXID * sizeof(unsigned int));
		g_cosInheritTotal = g_cosInheritOverlap = 0;
	}

	// Cosmetic events: what the event route sends (mode 2) or would send
	// (mode 1), against what the routed actors cost the snapshot now.
	if ((fua_snap_cosmetic_events > 0) && (g_stats.simTics > 0))
	{
		const double t = (double)g_stats.simTics;
		const double ct = (g_ceClientTics > 0) ? (double)g_ceClientTics : 1.0;
		QWORD generic = 0, todayBytes = 0, todayRecs = 0;
		for (unsigned int r = 0; r < SNAPCOS_ROWS; ++r)
		{
			generic += g_ceGeneric[r];
			todayBytes += g_ceTodayBytes[r];
			todayRecs += g_ceTodayRecs[r];
		}
		const QWORD seen = g_ceKept + g_ceCulled;
		snap_Say("[snap] cosmetic events: %.1f/tic made - rows 1-5 %.1f %.1f %.1f %.1f %.1f,"
			" blood %.1f, splatter %.1f | per client %.0f B/tic after culling (%.0f%% culled), against %.0f B/tic"
			" of snapshot records today (%.1f records/tic; rows 1-5 %.0f %.0f %.0f %.0f %.0f B/tic)\n",
			(double)(generic + g_ceBlood + g_ceSplatter) / t,
			(double)g_ceGeneric[1] / t, (double)g_ceGeneric[2] / t, (double)g_ceGeneric[3] / t,
			(double)g_ceGeneric[4] / t, (double)g_ceGeneric[5] / t,
			(double)g_ceBlood / t, (double)g_ceSplatter / t,
			(double)g_ceWanted / ct, (seen > 0) ? 100.0 * (double)g_ceCulled / (double)seen : 0.0,
			(double)todayBytes / ct, (double)todayRecs / ct,
			(double)g_ceTodayBytes[1] / ct, (double)g_ceTodayBytes[2] / ct, (double)g_ceTodayBytes[3] / ct,
			(double)g_ceTodayBytes[4] / ct, (double)g_ceTodayBytes[5] / ct);

		const QWORD sentPumps = g_ceAppended + g_ceAlone;
		snap_Say("[snap] cosmetic events cap: %d B/tic budget, %u B datagram | %.0f%% of client pumps capped"
			" (bank %.0f B when capped), %.0f B/tic sent | carried %.1f events/tic, dropped %.1f/tic (%.1f past age 7,"
			" %.1f over again, %.1f past a datagram) | %.0f%% join the last chunk, %.0f%% alone\n",
			(int)fua_snap_cosmetic_bytes, snap_PacketRoom() - SNAP_COSEVENT_HEADER,
			(g_ceClientPumps > 0) ? 100.0 * (double)g_cePumpsCapped / (double)g_ceClientPumps : 0.0,
			(g_cePumpsCapped > 0) ? (double)g_ceBankAtCap / (double)g_cePumpsCapped : 0.0,
			(double)g_ceSent / ct, (double)g_ceCarried / ct,
			(double)(g_ceCarryAged + g_ceCarryOver + g_ceCapDropped) / ct,
			(double)g_ceCarryAged / ct, (double)g_ceCarryOver / ct, (double)g_ceCapDropped / ct,
			(sentPumps > 0) ? 100.0 * (double)g_ceAppended / (double)sentPumps : 0.0,
			(sentPumps > 0) ? 100.0 * (double)g_ceAlone / (double)sentPumps : 0.0);
		snap_Say("[snap] cosmetic events hold: %.1f/tic held | released after the first Tick %.1f, by a script %.1f,"
			" at the deadline %.1f | removed while held %.1f | script changes after the event %.1f/tic | %u held now\n",
			(double)g_ceHeldN / t, (double)g_ceRelTick / t, (double)g_ceRelScript / t, (double)g_ceRelDeadline / t,
			(double)g_ceRemovedHeld / t, (double)g_ceScriptAfter / t, g_ceHeld.Size());

		// The not-inert flagged class spawned most (rows 2 and 4).
		unsigned int topId = 0, topN = 0;
		QWORD notInert = 0;
		if (g_ceNotInertByClass != NULL)
			for (unsigned int c = 0; c < SNAP_MAXID; ++c)
			{
				notInert += g_ceNotInertByClass[c];
				if (g_ceNotInertByClass[c] > topN)
				{
					topN = g_ceNotInertByClass[c];
					topId = c;
				}
			}
		const PClass* pTop = (topN > 0) ? NETWORK_GetClassFromIdentification((unsigned short)topId) : NULL;
		const double g = (generic > 0) ? (double)generic : 1.0;
		snap_Say("[snap] cosmetic events detail: velocity over 32 u/tic %.0f%%, under 1 %.0f%% | state byte %.0f%%"
			" (%.0f%% outside the chain) | extras %.0f%% | shooter skips %.1f/tic | age past 7 %.0f%% | from native Tick %.1f/tic"
			" | made by the spawner's copy %.1f/tic | gone before the flush %.1f/tic | untold puffs %.1f/tic"
			" | server copies removed %.1f/tic"
			" | relaxed missiles %.1f/tic | flagged, not inert %.1f/tic%s%s | blood originator unknown %.0f%%\n",
			100.0 * (double)g_ceVelBig / g, 100.0 * (double)g_ceVelSmall / g,
			100.0 * (double)g_ceStateByte / g,
			(g_ceStateByte > 0) ? 100.0 * (double)g_ceStateOutside / (double)g_ceStateByte : 0.0,
			100.0 * (double)g_ceExtras / g, (double)g_ceShooterSkips / ct,
			(g_ceKept > 0) ? 100.0 * (double)g_ceAgeSaturated / (double)g_ceKept : 0.0,
			(double)g_ceFromTick / t, (double)g_ceByCopy / t, (double)g_ceDiedEarly / t, (double)g_ceUntold / t,
			(double)g_ceRemoved / t,
			(double)g_ceRelaxed / t, (double)notInert / t,
			(pTop != NULL) ? ", top " : "", (pTop != NULL) ? pTop->TypeName.GetChars() : "",
			(g_ceBloodKept > 0) ? 100.0 * (double)g_ceBloodUnknown / (double)g_ceBloodKept : 0.0);

		// Layer 1 (layer-1 note): map things clients could make, the TID rule,
		// ACS-named classes and routed copies that outlive their event.
		char szListed[256], szSolid[256], szAge[256];
		snap_CosTop3(g_cmListedByClass, szListed, sizeof(szListed));
		snap_CosTop3(g_cmSolidByClass, szSolid, sizeof(szSolid));
		snap_Say("[snap] cosmetic map things: %u of %u networked map things clients could make%s%s%s, %u live"
			" | refused: marker %u, tid %u, special %u, args %u | solid, failing nothing else, %u%s%s%s"
			" | per client today: the listed %.1f records/tic, %.0f B/tic; the solid %.1f records/tic, %.0f B/tic\n",
			g_cmListed, g_cmNetworked, (szListed[0] != '\0') ? " (top: " : "", szListed,
			(szListed[0] != '\0') ? ")" : "", g_cmLive,
			g_cmRefused[0], g_cmRefused[1], g_cmRefused[2], g_cmRefused[3],
			g_cmSolidOnly, (szSolid[0] != '\0') ? " (top: " : "", szSolid, (szSolid[0] != '\0') ? ")" : "",
			(double)g_cmTodayRecs / ct, (double)g_cmTodayBytes / ct,
			(double)g_cmSolidRecs / ct, (double)g_cmSolidBytes / ct);
		unsigned int past5 = 0, past30 = 0, tidNow = 0;
		if (g_ceAgeByClass != NULL)
			memset(g_ceAgeByClass, 0, SNAP_MAXID * sizeof(unsigned int));
		const unsigned int alive = (g_ceAgeByClass != NULL) ? snap_CosCountAlive(&past5, &past30, &tidNow) : 0;
		snap_CosTop3(g_ceAgeByClass, szAge, sizeof(szAge));
		const QWORD tidRefused = g_ceTidRule[1] + g_ceTidRule[2] + g_ceTidRule[3];
		snap_Say("[snap] cosmetic layer 1: the TID rule refuses %.1f/tic at the flush (tid %.1f, special %.1f,"
			" args %.1f), %.1f/tic children made by a refused copy's client copy | %u routed copies hold a tid now"
			" | ACS spawns routed %.1f/tic (%.1f +CLIENTSIDEONLY) | ACS names %u classes, %u keep a server copy"
			" (%.1f such copies/tic) | %u routed copies alive,"
			" %u past 5 s, %u past 30 s%s%s%s | %u joins, %.0f alive at each (worst %u)\n",
			(double)tidRefused / t, (double)g_ceTidRule[1] / t, (double)g_ceTidRule[2] / t,
			(double)g_ceTidRule[3] / t, (double)g_ceOrphaned / t, tidNow,
			(double)g_ceAcsRouted / t, (double)g_ceAcsClientSide / t,
			g_cosAcsClasses, g_cosAcsRow1, (double)g_ceAcsKept / t, alive, past5, past30,
			(szAge[0] != '\0') ? " (top: " : "", szAge, (szAge[0] != '\0') ? ")" : "",
			(unsigned int)g_ceJoins, (g_ceJoins > 0) ? (double)g_ceJoinAlive / (double)g_ceJoins : 0.0,
			(unsigned int)g_ceJoinWorst);
		snap_CosCountersReset();
	}

	// Reset here, with the unconditional print above, not in the block below.
	if (g_popByClass != NULL)
	{
		memset(g_popByClass, 0, SNAP_MAXID * sizeof(unsigned int));
		memset(g_popSettledByClass, 0, SNAP_MAXID * sizeof(unsigned int));
	}
	g_popTotal = g_popSettledTotal = 0;

	// Compressed only while recording, so a 0 never stands for a compressor
	// that was off.
	if (g_recordFile != NULL)
	{
		snap_Say("[snap] delta avg=%dB zstd=%dB (peak %uB/%uB) | %.1f dirty/frame @ %.1fB | budget %dB fits ~%d ents (%.1f kB/s @ %d Hz)\n",
			(int)avgDelta, (int)avgZstd, g_stats.peakDelta, g_stats.peakZstd,
			avgDirty, bytesPerDirty, (int)budget, entsPerBudget,
			avgZstd * TICRATE / 1024.0, TICRATE);
	}
	else
	{
		snap_Say("[snap] delta avg=%dB zstd=off (peak %uB) | %.1f dirty/frame @ %.1fB | budget %dB fits ~%d ents (%.1f kB/s uncompressed @ %d Hz)\n",
			(int)avgDelta, g_stats.peakDelta,
			avgDirty, bytesPerDirty, (int)budget, entsPerBudget,
			avgDelta * TICRATE / 1024.0, TICRATE);
	}

	// Only off origin 0, wide or when something clamped, so a map that fits
	// prints what it always has.
	if ((g_frameClamps > 0)
		|| ((g_mapFrame.origin[0] | g_mapFrame.origin[1] | g_mapFrame.origin[2]) != 0)
		|| ((g_mapFrame.width[0] | g_mapFrame.width[1] | g_mapFrame.width[2]) != SNAP_FRAME_NARROW))
	{
		snap_Say("[snap] frame: origin %d %d %d, %u %u %u bytes an axis | %u positions clamped\n",
			g_mapFrame.origin[0], g_mapFrame.origin[1], g_mapFrame.origin[2],
			g_mapFrame.width[0], g_mapFrame.width[1], g_mapFrame.width[2], g_frameClamps);
	}

	// Where the dirty bytes go, by group, as a share of dirty records.
	if (g_stats.dirtyRecords > 0)
	{
		// In snap_TallyGroups' order, or every column shifts.
		static const char* const szGroup[SNAP_GROUP_COUNT] = { "pos", "posz", "ang", "velmisc", "appear", "render" };
		QWORD totalGrpBytes = 0;
		for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
			totalGrpBytes += g_stats.grpBytes[g];

		char szHist[256];
		size_t at = 0;
		szHist[0] = '\0';
		for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
		{
			const double recPct = 100.0 * (double)(QWORD)g_stats.grpRecs[g]
				/ (double)(QWORD)g_stats.dirtyRecords;
			const double bytePct = (totalGrpBytes > 0)
				? (100.0 * (double)(QWORD)g_stats.grpBytes[g] / (double)(QWORD)totalGrpBytes)
				: 0.0;
			const int wrote = mysnprintf(szHist + at, sizeof(szHist) - at,
				"%s%s %.0f%%/%.0f%%", (g > 0) ? " | " : "", szGroup[g], recPct, bytePct);
			if ((wrote <= 0) || ((size_t)wrote >= sizeof(szHist) - at))
				break;
			at += (size_t)wrote;
		}
		// Enters, left out of the histogram, are printed, so an all-enter
		// window's zeros read as such.
		if (g_brTotal > 0)
		{
			// The rate is world-wide, the histograms are per client: obs/branch
			// should read about the active client count.
			const double brObs = (g_branchCount > 0)
				? (double)g_brTotal / (double)g_branchCount : 0.0;
			snap_Say("[snap] branches: %.1f/frame world (%.1f/frame re-entry, %.1f/frame new"
				" occupant) | %u obs (%.1f/branch)"
				" | dist <512:%u <1k:%u <2k:%u <4k:%u <8k:%u far:%u\n",
				(double)g_branchCount / (double)(g_stats.frames > 0 ? g_stats.frames : 1),
				(double)g_reentryCount / (double)(g_stats.frames > 0 ? g_stats.frames : 1),
				(double)g_reentryReuse / (double)(g_stats.frames > 0 ? g_stats.frames : 1),
				g_brTotal, brObs,
				g_brDist[0], g_brDist[1], g_brDist[2], g_brDist[3], g_brDist[4], g_brDist[5]);
			snap_Say("[snap] branchpos (per-client obs): stale <2t:%u <5t:%u <10t:%u <35t:%u 35t+:%u"
				" | still:%u slow:%u fast:%u | deaths %u (%u within 512)\n",
				g_brStale[0], g_brStale[1], g_brStale[2], g_brStale[3], g_brStale[4],
				g_brVel[0], g_brVel[1], g_brVel[2], g_brDeaths, g_brDeathsNear);
			{
				unsigned int evQ, evS, evR, evXU, evXA, evF, evB, evSU, evSS;
				ServerEventStats(evQ, evS, evR, evXU, evXA, evF, evB, evSU, evSS);
				if ((evQ > 0) || (evS > 0))
					snap_Say("[snap] events: %u queued, %u sent, %u extra copies | "
						"EXPIRED unsent %u (share too small), acked-unknown %u | "
						"%u dropped full | superseded %u UNSENT, %u after"
						" delivery | %.1f kB/s\n",
						evQ, evS, evR, evXU, evXA, evF, evSU, evSS,
						(double)evB * (double)TICRATE / (1024.0 * (double)g_evSpanTics));

				{
					// Deaths apart, the one event failure visible in play.
					unsigned int evTS, evTX, evTR, evLU, evLP;
					EventTerminals(evTS, evTX, evTR, evLU, evLP);
					if ((evTS > 0) || (evTX > 0) || (evLU > 0) || (evLP > 0))
						snap_Say("[snap] event deaths: %u sent, %u spaced copies, %u EXPIRED"
							" unsent | at their entity's leave, deaths dropped: %u NEVER SENT,"
							" %u with later copies cut\n",
							evTS, evTR, evTX, evLU, evLP);
				}

				{
					unsigned int evV, evSD, evSF, evSS;
					EventSkips(evV, evSD, evSF, evSS);
					unsigned int evEmits, evSpentMax, evQMax, evBrk;
					QWORD evBudSum, evShareSum, evQSum;
					EventEmitStats(evEmits, evBudSum, evShareSum, evSpentMax, evQSum,
						evQMax, evBrk);
					// Per emit with events queued (one client, one
					// frame); spent is evB, over the same emits.
					if (evEmits > 0)
						snap_Say("[snap] event emit: %u emits | budget %.0f -> share %.0f B,"
							" spent %.0f (%.0f%% of share), max %u | queue %.1f, max %u"
							" | share stopped the loop in %u\n",
							evEmits, (double)evBudSum / evEmits, (double)evShareSum / evEmits,
							(double)evB / evEmits,
							(evShareSum > 0) ? 100.0 * (double)evB / (double)evShareSum : 0.0,
							evSpentMax, (double)evQSum / evEmits, evQMax, evBrk);

					if (evV > 0)
						snap_Say("[snap] event skips: %u visits | %u done, %u same-frame, "
							"%u share-blocked\n",
							evV, evSD, evSF, evSS);
				}
				ServerEventStatsReset();
				g_evSpanTics = 0;
			}


			if (g_straightSamples > 0)
			{
				snap_Say("[snap] straight runs (tics on one heading): missile 1:%u 2-3:%u"
					" 4-7:%u 8-15:%u 16+:%u | other 1:%u 2-3:%u 4-7:%u 8-15:%u 16+:%u\n",
					g_runMissile[0], g_runMissile[1], g_runMissile[2], g_runMissile[3],
					g_runMissile[4], g_runOther[0], g_runOther[1], g_runOther[2],
					g_runOther[3], g_runOther[4]);
				// Last tic's bit against this tic's heading: pred-TURNED is the
				// error that matters, step-held the opportunity cost.
				snap_Say("[snap] ballistic prediction: pred-held %u, PRED-TURNED %u, "
					"step-held %u, step-turned %u (%u missile samples, %.0f%% of "
					"extrapolated tics correct)\n",
					g_predHeld, g_predTurned, g_stepHeld, g_stepTurned,
					g_straightSamples,
					((g_predHeld + g_predTurned) > 0)
					? (100.0 * (double)g_predHeld / (double)(g_predHeld + g_predTurned)) : 0.0);
			}

		}

		snap_Say("[snap] dirty groups (%% of records / %% of group bytes): %s"
			" | %.0f dirty scored, %.1f enters/frame excluded\n", szHist,
			(double)(QWORD)g_stats.dirtyRecords / (double)g_stats.frames,
			(double)(QWORD)g_stats.enters / (double)g_stats.frames);
	}

	// Where the pump's time went, by phase, `sim` being a tic count, not ms.
	if (g_phaseFrames != 0)
	{
		// Per game tic covered, as the tic budget is, with per pump and worst
		// in ms as stalls of their own.
		const double pinv = 1.0 / (double)g_phaseFrames;
		const double tinv = pinv / simRatio;
		const double capMS = snap_RawToMS(g_phaseCaptureRaw) * tinv;
		const double sendMS = snap_RawToMS(g_phaseSendRaw) * tinv;
		const double tickMS = snap_RawToMS(g_phaseTickRaw) * tinv;
		snap_Say("[snap] pump: %.2f ms/tic snapshot = capture %.2f + perclient %.2f"
			" | %.0f%% of the 28.6 ms budget | per pump %.2f ms, worst %.1f\n",
			tickMS, capMS, sendMS,
			100.0 * tickMS / (1000.0 / (double)TICRATE),
			snap_RawToMS(g_phaseTickRaw) * pinv,
			snap_RawToMS(g_phaseWorstRaw));
	}
	// The pump's period against 28.6 ms, and the previous report's cost.
	if (g_pumpGapCount > 0)
	{
		snap_Say("[snap] pump gap: mean %.1f ms, worst %.1f ms spanning %d tics"
			" | previous report took %.1f ms, %u skipped\n",
			snap_RawToMS(g_pumpGapSumRaw) / (double)g_pumpGapCount,
			snap_RawToMS(g_pumpGapWorstRaw), g_pumpGapWorstTics,
			snap_RawToMS(g_reportRaw), g_reportSkipped);
	}

	{
		// Psprite emission when it did anything, `capped` nonzero if clipped.
		unsigned int psEmit = 0, psCap = 0;
		ServerPSpriteStats(psEmit, psCap, true);
		if ((psEmit | psCap) != 0)
			snap_Say("[snap] psprite emit: %.2f records/frame across all clients"
				" | %u client-frames capped at %d\n",
				(f > 0.0) ? ((double)psEmit / f) : 0.0, psCap,
				(int)SNAPCL_PSPRITE_MAX_REPORT);

		// The local block whenever the pass ran, records a frame expected to
		// equal the client count, with `auth` (not `cfg`) what it did (10.1).
		SnapLocalEmitStats lo;
		ServerLocalStats(lo, true);
		if ((lo.emitted | lo.noPawn | lo.spectator | lo.noClient | lo.noId) != 0)
			snap_Say("[snap] local block: %.2f records/frame across all clients"
				" | auth %u/%u | cfg nolegacy %d"
				" | %u no-pawn | %u spectator | %u no-client | %u no-id\n",
				(f > 0.0) ? ((double)lo.emitted / f) : 0.0,
				lo.authoritative, lo.emitted, (int)fua_snap_nolegacy,
				lo.noPawn, lo.spectator, lo.noClient, lo.noId);

		// Movers, unconditionally (9.2), `changed` near `captures` or a binding
		// `capped` meaning the plane is being compared.
		{
			unsigned int mvLive = 0, mvCap = 0, mvBump = 0, mvFull = 0;
			unsigned int mvEmit = 0, mvCapped = 0, mvBlk = 0, mvEmitBlk = 0;
			unsigned int mvStair = 0, mvCls[SNAPMV_COUNT] = { 0 };
			unsigned int mvRecyc = 0, mvRecycPlane = 0, mvDisowned = 0;
			unsigned int mvRetired = 0, mvNarrowed = 0;
			unsigned int mvNoBytes = 0, mvSweptUnstarted = 0, mvSweptPressure = 0;
			unsigned int mvHeldPast = 0, mvHeldLongest = 0, mvHeldNow = 0;
			MoverCaptureStats(mvLive, mvCap, mvBump, mvFull, mvBlk,
				mvRecyc, mvRecycPlane, mvDisowned, mvRetired, mvNarrowed,
				mvSweptUnstarted, mvSweptPressure, mvHeldPast, mvHeldLongest,
				mvHeldNow, true);
			ServerMoverStats(mvEmit, mvCapped, mvEmitBlk, mvStair, mvCls,
				mvNoBytes, true);
			unsigned int mvPolyReclaimed = 0, mvPolyOverCap = 0, mvPolyResyncs = 0;
			MoverPolyStats(mvPolyReclaimed, mvPolyOverCap, mvPolyResyncs, true);

			// Only the classes that emitted, so the line does not grow a zero
			// column per class.
			char szCls[128];
			size_t nCls = 0;
			szCls[0] = '\0';
			for (int i = 0; i < SNAPMV_COUNT; i++)
			{
				if ((mvCls[i] == 0) || (nCls + 1 >= sizeof(szCls)))
					continue;
				mysnprintf(szCls + nCls, sizeof(szCls) - nCls, " %s %u",
					ComputeMoverClassName((BYTE)i), mvCls[i]);
				// strlen, not the return: mysnprintf reports the IDEAL length.
				nCls = strlen(szCls);
			}
			if (nCls == 0)
				mysnprintf(szCls, sizeof(szCls), " none");

			snap_Say("[snap] movers: cfg fua_snap_movers %d | %u live | %u captures"
				" %u changed | %u emitted (%u stair) by class:%s"
				" | frames ended: %u at the %d ceiling, %u out of bytes"
				" | %u refused, table full"
				" | slot reuse %u by address, %u by plane, %u disowned,"
				" %u retired, %u narrowed"
				" | swept %u unstarted, %u under table pressure"
				" | held past the old limit: %u released after it, %u still held,"
				" longest %u frames"
				" | blocked %u captured, %u emitted"
				" | poly slots reclaimed %u, %u entries over cap, %u resyncs\n",
				( fua_snap_movers ? 1 : 0 ), mvLive, mvCap, mvBump, mvEmit,
				mvStair, szCls,
				mvCapped, SNAPCL_MOVER_MAX, mvNoBytes, mvFull, mvRecyc,
				mvRecycPlane, mvDisowned, mvRetired, mvNarrowed, mvSweptUnstarted,
				mvSweptPressure, mvHeldPast, mvHeldNow, mvHeldLongest, mvBlk,
				mvEmitBlk, mvPolyReclaimed, mvPolyOverCap,
				mvPolyResyncs);

			// The legacy mover gate (7.1), `stopped` 0 for a class in use
			// meaning a sender it misses.
			unsigned int gtStop[SNAPMV_COUNT], gtPlanes = 0, gtResent = 0, gtResets = 0;
			unsigned int sndSector = 0, sndLegacy = 0;
			ServerMoverGateStats(gtStop, gtPlanes, gtResent, gtResets, true);
			MoverSoundStats(sndSector, sndLegacy, true);
			snap_Say("[snap] legacy movers: gate %d | stopped plat %u, floor %u,"
				" elevator %u, pillar %u, ceiling %u, door %u, poly %u"
				" (%u plane writes) | %u re-sent at gate off | %u table resets"
				" | mover sounds %u by sector, %u legacy\n",
				( MoverGate( ) ? 1 : 0 ), gtStop[SNAPMV_PLAT], gtStop[SNAPMV_FLOOR],
				gtStop[SNAPMV_ELEVATOR], gtStop[SNAPMV_PILLAR], gtStop[SNAPMV_CEILING],
				gtStop[SNAPMV_DOOR], gtStop[SNAPMV_POLY], gtPlanes, gtResent, gtResets,
				sndSector, sndLegacy);
		}

		// The MovePlayer gate, unconditionally, 0 with the cvar on meaning it
		// is not reached.
		{
			const QWORD skips = LegacyPlayerSkips(true);
			snap_Say("[snap] legacy MovePlayer: %llu client-frames suppressed\n",
				(unsigned long long)skips);
		}

		// Own-missile enter latency whenever a player fired, zero waits too.
		{
			unsigned int oh[SNAPCL_OWNENTER_BUCKETS_REPORT];
			unsigned int oN = 0, oW = 0, oSpawn = 0, oEvict = 0, oResent = 0;
			OwnEnterStats(oh, oN, oW, true);
			OwnMissileStats(oSpawn, oEvict, oResent, true);
			if ((oN | oSpawn) != 0)
			{
				const double inv = (oN > 0) ? (100.0 / (double)oN) : 0.0;
				snap_Say("[snap] own missile: %u spawned, %u enters sent"
					" | wait 0t %.0f%% 1t %.0f%% 2t %.0f%% 3-4t %.0f%%"
					" 5-8t %.0f%% 9t+ %.0f%% (worst %u tics)"
					" | %u re-sent | %u evicted\n",
					oSpawn, oN,
					oh[0] * inv, oh[1] * inv, oh[2] * inv,
					oh[3] * inv, oh[4] * inv, oh[5] * inv,
					oW, oResent, oEvict);
			}
		}
	}


	// Immediately after its own print, and outside every gate below, so no
	// gate can skip the reset.
	g_phaseCaptureRaw = g_phaseSendRaw = g_phaseTickRaw = g_phaseWorstRaw = 0;
	g_phaseFrames = 0;
	g_pumpGapSumRaw = g_pumpGapWorstRaw = g_reportRaw = 0;
	g_pumpGapCount = 0;
	g_pumpGapWorstTics = 0;
	g_reportSkipped = 0;

	if (g_sentPackets != 0)
	{
		snap_Say("[snap] sent %u packets, %.1f kB (%.1f kB/s)\n",
			g_sentPackets, g_sentBytes / 1024.0,
			(g_sentBytes / 1024.0) / gameSec);

		// [Kaori-Chani] Every client in turn, not slot 0: one client settling
		// says nothing about another's, and slot 0 may be the one that left.
		for (unsigned int c = 0; c < MAXPLAYERS; ++c)
		{
			if (SERVER_IsValidClient(c) == false)
				continue;
			// lag is how far behind this client's newest datagram its ack is,
			// where 255 is far or further and 0 is none credited: either none has
			// arrived or a re-baseline has just forgotten the world.
			snap_Say("[snap] client %u: ack=%u lag=%u pending=%u believed=%u\n",
				c, LastAckedSeq(c), (unsigned int)AckLag(c, NewestSentSeq(c)),
				PendingFrames(c), BelievedCount(c));
		}

		// Every client's health at once, max pending the signal and any
		// rebaseline worth investigating.
		SnapClientHealth health;
		AggregateHealth(health);
		snap_Say("[snap] clients: %u active | believed avg %u max %u | "
			"pending avg %.1f max %u | %u rebaselines\n",
			health.activeClients, health.believedAvg, health.believedMax,
			health.pendingAvg, health.pendingMax, health.rebaselines);

		// The contested set by distance ring and by in-front, which is what
		// sizes interest management.
		if (g_priorityFrames != 0)
		{
			SnapVisibilityCensus c;
			GetCensus(c);
			const double inv = 1.0 / (double)g_priorityFrames;

			// Fixed scratch, its cursor clamped, since mysnprintf returns
			// the would-be length.
			char dbuf[128]; unsigned int dn = 0;
			char fbuf[128]; unsigned int fn = 0;
			for (int i = 0; i < c.buckets; ++i)
			{
				if (dn < sizeof(dbuf) - 1)
				{
					const int w = mysnprintf(dbuf + dn, sizeof(dbuf) - dn, "%s%.0f",
						(i ? " " : ""), (double)c.byDist[i] * inv);
					if (w > 0)
						dn += (unsigned int)w;
				}
				if (fn < sizeof(fbuf) - 1)
				{
					const int w = mysnprintf(fbuf + fn, sizeof(fbuf) - fn, "%s%.0f",
						(i ? " " : ""), (double)c.frontByDist[i] * inv);
					if (w > 0)
						fn += (unsigned int)w;
				}
			}
			if (dn >= sizeof(dbuf)) dbuf[sizeof(dbuf) - 1] = 0;
			if (fn >= sizeof(fbuf)) fbuf[sizeof(fbuf) - 1] = 0;

			snap_Say("[snap] census: %.0f contested/frame, %.0f in front (%.0f%%) | "
				"by ring [%s] | front by ring [%s] (rings 512/1024/2048/4096/8192)\n",
				(double)c.contested * inv, (double)c.front * inv,
				(c.contested > 0) ? (100.0 * (double)c.front / (double)c.contested) : 0.0,
				dbuf, fbuf);
		}

		// The refresh rate an entity sees, averaged over the whole world; the
		// per-band line below answers the exit criterion.
		if (g_priorityFrames != 0)
		{
			const double inv = 1.0 / (double)g_priorityFrames;

			double emittedRecs = 0.0;
			for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				emittedRecs += (double)g_tierAccum[t] * inv;

			// For the deferred/withheld reporting below, not for `contested`.
			const double deferredRecs = (double)(QWORD)g_deferredAccum * inv;
			const double withheld = (double)(QWORD)g_cosmeticAccum * inv;
			(void)deferredRecs; (void)withheld;

			// Everything ranked and competing, summed directly over the
			// ranked tiers rather than by difference.
			double contested = 0.0;
			for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				contested += (double)g_tierSizeAccum[t] * inv;
			const double fresh = (double)(QWORD)g_freshAccum * inv;
			// `rate` against `fresh` (its new share): a low rate is a budget
			// problem, a low fresh share an ack one.
			const double rate = (contested > 1.0)
				? (emittedRecs * pumpsPerSec / contested) : pumpsPerSec;
			const double freshShare = (emittedRecs > 0.0) ? (100.0 * fresh / emittedRecs) : 0.0;

			// `resend`, records carrying only unacked groups, whose bytes a
			// shorter ack round trip would buy back.
			const double resendRecs = (double)(QWORD)g_resendAccum * inv;
			const double resendB = (double)(QWORD)g_resendByteAccum * inv;

			snap_Say("[snap] refresh: %.1f Hz per contested entity (%.0f contested, "
				"%.1f rec/frame, %.0f%% fresh) | resend %.1f rec/f (%.0f B/f) "
				// Injected loss and delay; observed loss: datagram delivery.
				"| inject ackdelay %d, loss %d%% (%u dropped), holdback %d%% (%u held)\n",
				rate, (contested > 0.0) ? contested : 0.0, emittedRecs, freshShare,
				resendRecs, resendB,
				(int)fua_snap_debug_ackdelay, (int)fua_snap_debug_loss, g_droppedPackets,
				(int)fua_snap_debug_holdback, g_heldPackets);

			// Per-band refresh, the exit criterion's quantity: records the band
			// sent over entities ranked into it.
			{
				char szRate[256];
				size_t at = 0;
				szRate[0] = '\0';
				for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				{
					const double sz = (double)(QWORD)g_tierSizeAccum[t] * inv;
					if (sz < 0.5)
						continue;			// band empty this window; omit it
					const double rec = (double)(QWORD)g_tierAccum[t] * inv;
					const double hz = rec * pumpsPerSec / sz;
					const int wrote = mysnprintf(szRate + at, sizeof(szRate) - at,
						"%s%s %.1f/%.0f", (at > 0) ? " | " : "", g_szTierName[t], hz, sz);
					if ((wrote <= 0) || ((size_t)wrote >= sizeof(szRate) - at))
						break;
					at += (size_t)wrote;
				}
				if (at > 0)
					snap_Say("[snap] band Hz/entities: %s\n", szRate);

				// Reserve use: `spent` under `allowed` wastes d0's bytes, and
				// `capped` bands bind as designed, `drained` ones oversized.
				if (g_resAllowAccum != 0)
				{
					const double allow = (double)(QWORD)g_resAllowAccum * inv;
					const double spent = (double)(QWORD)g_resSpentAccum * inv;
					snap_Say("[snap] reserve: %.0f B allowed, %.0f B spent (%.0f%%)"
						" | bands capped %.1f, drained %.1f\n",
						allow, spent, (allow > 0.0) ? (100.0 * spent / allow) : 0.0,
						(double)(QWORD)g_resCappedAccum * inv,
						(double)(QWORD)g_resDrainedAccum * inv);

				}
			}
		}

		// Both channels together: the only figure that shows what the legacy
		// suppression gate is worth.
		if (g_haveOutboundMark)
		{
			const QWORD total = SERVER_STATISTIC_GetTotalOutboundDataTransferred();
			const double windowBytes = (double)(QWORD)(total - g_outboundAtWindowStart);
			const double snapshotBytes = (double)(QWORD)g_wireSentBytes;
			const double legacyShare = (windowBytes > 0.0)
				? (100.0 * (windowBytes - snapshotBytes) / windowBytes) : 0.0;

			snap_Say("[snap] server outbound: %.1f kB/s total | snapshot %.1f kB/s | "
				"everything else %.1f kB/s (%.0f%%) | nolegacy %d\n",
				(windowBytes / 1024.0) / gameSec,
				(snapshotBytes / 1024.0) / gameSec,
				((windowBytes - snapshotBytes) / 1024.0) / gameSec,
				legacyShare, (int)fua_snap_nolegacy);
		}
		snap_ReportLegacyTraffic(gameSec);

		// What those packets cost on the wire, and so the room the MTU leaves.
		if (g_wirePackets != 0)
		{
			const double payload = (double)(QWORD)g_wirePayloadBytes / (double)g_wirePackets;
			const double wire = (double)(QWORD)g_wireSentBytes / (double)g_wirePackets;
			const double ratio = (wire > 0.0) ? (payload / wire) : 0.0;
			const unsigned int mtu = (unsigned int)SERVER_GetMaxPacketSize();

			// Per DATAGRAM: how much bigger a CHUNK could be, not the budget,
			// which grows by adding datagrams.
			const int headroom = (ratio > 0.0)
				? (int)((double)mtu * ratio) - SNAP_PACKET_OVERHEAD : 0;

			snap_Say("[snap] wire: %.0fB payload -> %.0fB sent (%.2fx, peak %uB, zstd %u/%u) "
				"| %.1f kB/s actual | chunk could be ~%dB at sv_maxpacketsize %u\n",
				payload, wire, ratio, g_wirePeak, g_wireZstdPackets, g_wirePackets,
				((double)(QWORD)g_wireSentBytes / 1024.0) / gameSec,
				(headroom > 64) ? headroom : 64, mtu);
		}

		// `chunks` built and datagrams `launched` must be equal, or chunks
		// shared a datagram.
		if (g_chunkFrames != 0)
			snap_Say("[snap] chunking: %.2f chunks/frame, %llu chunks vs %llu launched "
				"| budget %uB over chunks of %uB\n",
				(double)(QWORD)g_chunkDatagrams / (double)(QWORD)g_chunkFrames,
				(unsigned long long)g_chunkDatagrams,
				(unsigned long long)g_wirePackets,
				snap_EffectiveBudget(), snap_ChunkLimit());

		// Where the budget went, averaged over the window.
		if (g_priorityFrames != 0)
		{
			const double inv = 1.0 / (double)g_priorityFrames;
			double emitted = 0;
			for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				emitted += (double)g_tierAccum[t] * inv;

			// One entry per tier, asserted against the count rather than a
			// literal, so only adding the name satisfies it.
			typedef char snapTierNamesMatchTiers
				[(sizeof(g_szTierName) / sizeof(g_szTierName[0]) == SNAPCL_TIERS_REPORT) ? 1 : -1];
			(void)sizeof(snapTierNamesMatchTiers);

			// Built from g_szTierName rather than a format string, so no band
			// goes unprinted.
			{
				char szPri[320];
				size_t at = 0;
				szPri[0] = '\0';
				for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				{
					const int wrote = mysnprintf(szPri + at, sizeof(szPri) - at,
						"%s%s %.0f", (t > 0) ? " " : "", g_szTierName[t],
						(double)g_tierAccum[t] * inv);

					if ((wrote <= 0) || ((size_t)wrote >= sizeof(szPri) - at))
						break;

					at += (size_t)wrote;
				}
				 snap_Say("[snap] priority/frame: %s = %.0f recs\n", szPri, emitted);
			}

			// A high forced share means the budget is so oversubscribed that
			// priority has degenerated toward round-robin.
			snap_Say("[snap] sound events: %u sent (%u positional, %u from routed cosmetics), %u culled (%.0f%% inaudible)\n",
			g_soundSent, g_soundPositional, g_soundRoutedPoint, g_soundCulled,
			// Widen BEFORE adding: the sum of two unsigned ints is computed in
			// 32 bits and only then converted (MSVC C26451).
			((double)g_soundSent + (double)g_soundCulled > 0.0)
				? (100.0 * (double)g_soundCulled
					/ ((double)g_soundSent + (double)g_soundCulled)) : 0.0);
		g_soundSent = g_soundCulled = g_soundPositional = g_soundRoutedPoint = 0;

		// Per client, for this map's stamp: matched takes ids, DIFFER keeps
		// names for the whole map, unanswered has not acked since the table.
		{
			unsigned int matched = 0, differ = 0, unanswered = 0;
			for (unsigned int i = 0; i < MAXPLAYERS; ++i)
			{
				if (snap_ClientInMap(i) == false)
					continue;
				const BYTE a = g_soundIdsAnswer[i];
				if (((a & SNAP_SOUNDIDS_ANSWERED) == 0)
					|| ((a & SNAP_SOUNDIDS_STAMP) != (g_snapMapStamp & SNAP_SOUNDIDS_STAMP)))
					++unanswered;
				else if (a & SNAP_SOUNDIDS_MATCH)
					++matched;
				else
					++differ;
			}
			snap_Say("[snap] sound ids: %u by id, %u by name (%u id range, %u channel,"
				" %u switch off, %u client not matched) | clients %u matched,"
				" %u DIFFER (names), %u unanswered | fua_snap_soundids %d\n",
				g_soundById, g_soundByName, g_soundNameRange, g_soundNameChannel,
				g_soundNameOff, g_soundNameUnmatched,
				matched, differ, unanswered, (int)fua_snap_soundids);
			// Never audible anywhere, per S_Sound call before the fan-out,
			// apart from culled.
			snap_Say("[snap] sounds screened: %u unplayable calls (no sound or volume 0)\n",
				g_soundUnplayable);
			g_soundById = g_soundByName = 0;
			g_soundNameRange = g_soundNameChannel = g_soundNameOff = g_soundNameUnmatched = 0;
			g_soundUnplayable = 0;
		}

		g_branchCount = g_brTotal = g_brDeaths = g_brDeathsNear = 0;
		g_reentryCount = g_reentryReuse = 0;
		memset(g_brDist, 0, sizeof(g_brDist));
		memset(g_brStale, 0, sizeof(g_brStale));
		memset(g_brVel, 0, sizeof(g_brVel));
		memset(g_runMissile, 0, sizeof(g_runMissile));
		memset(g_runOther, 0, sizeof(g_runOther));
		g_predHeld = g_predTurned = g_stepHeld = 0;
		g_stepTurned = g_straightSamples = 0;

		unsigned int tSeen = 0, tProm = 0, tCap = 0, tEmit = 0;
		TerminalStats(tSeen, tProm, tCap, tEmit, true);
		if ((tSeen | tProm | tCap | tEmit) != 0)
		{
			snap_Say("[snap] terminal sequences: %u flagged, %u promoted, %u over"
				" ceiling, %u EMITTED (max %d/frame)\n",
				tSeen, tProm, tCap, tEmit, (int)SNAPCL_TERMINAL_MAX);
		}

		// Outside the death gate above, since both mechanisms run every
		// frame and their stats calls reset.
		{
			unsigned int pHeld, pSent, pExp, pWorst;
			PredictStats(pHeld, pSent, pExp, pWorst, true);
			unsigned int lHeld, lPos, lVel, lExp, lWorst;
			ServerLawStats(lHeld, lPos, lVel, lExp, lWorst, true);
			// Printed whenever the cvar is on, INCLUDING at zero, so "on and
			// suppressing nothing" and "off" are not one line.
			if (fua_snap_missile_predict > 0)
				snap_Say("[snap] missile predict: %u held, %u sent (drifted),"
					" %u past bridge | worst held %uu, tolerance %du | gravity class: %u held,"
					" sent on position %u, on velocity %u, %u past bridge, worst held %uu\n",
					pHeld, pSent, pExp, pWorst, (int)fua_snap_missile_predict,
					lHeld, lPos, lVel, lExp, lWorst);
		}

		{
			unsigned int aHeld[SNAPCL_APOP_COUNT], aDiv[SNAPCL_APOP_COUNT];
			unsigned int aUnw[SNAPCL_APOP_COUNT];
			AppearStats(aHeld, aDiv, aUnw, true);
			// Whenever the cvar is on, a line per population, as a share of
			// the decisions, which the client's probe cannot all see.
			if (fua_snap_appear_predict)
			{
				for (unsigned int ap = 0; ap < SNAPCL_APOP_COUNT; ++ap)
				{
					const unsigned int aTot = aHeld[ap] + aDiv[ap] + aUnw[ap];
					snap_Say("[snap] appear predict %s: %u held (%u%%),"
						" %u diverged, %u unwalkable of %u decisions\n",
						SNAPCL_AppearPopNames[ap], aHeld[ap],
						aTot ? (aHeld[ap] * 100 / aTot) : 0, aDiv[ap],
						aUnw[ap], aTot);
				}
			}
		}

		{
			unsigned int rSeen = 0, rHist[5];
			ReentryStats(rSeen, rHist, true);
			if (rSeen != 0)
				snap_Say("[snap] re-entry: %u returned from held"
					" | silence <1s %u <3s %u <6s %u <12s %u 12s+ %u\n",
					rSeen, rHist[0], rHist[1], rHist[2], rHist[3], rHist[4]);

			{
				unsigned int bRecs[SNAPCL_TIERS_REPORT], bPos[SNAPCL_TIERS_REPORT];
				BandComposition(bRecs, bPos, true);
				char szBand[512];
				size_t at = 0;
				bool any = false;
				for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				{
					if (bRecs[t] == 0)
						continue;
					any = true;
					const int wrote = snprintf(szBand + at, sizeof(szBand) - at,
						"%s%s %.0f%%", (at > 0) ? " | " : "", g_szTierName[t],
						100.0 * (double)bPos[t] / (double)bRecs[t]);
					if ((wrote <= 0) || ((size_t)wrote >= sizeof(szBand) - at))
						break;
					at += (size_t)wrote;
				}
				if (any)
					snap_Say("[snap] band POS share: %s\n", szBand);
			}

			unsigned int oG[SNAP_GROUP_COUNT], oS[4], oP[4], oTot = 0;
			OwedStats(oG, oS, oP, oTot, true);
			if (oTot > 0)
			{
				snap_Say("[snap] owed groups: pos %.0f%% posz %.0f%% ang %.0f%%"
					" velmisc %.0f%% appear %.0f%% render %.0f%% (%u contested)\n",
					100.0 * oG[0] / oTot, 100.0 * oG[1] / oTot, 100.0 * oG[2] / oTot,
					100.0 * oG[3] / oTot, 100.0 * oG[4] / oTot, 100.0 * oG[5] / oTot,
					oTot / (g_stats.frames ? g_stats.frames : 1));

				snap_Say("[snap] owed age: <1s %.0f%% <3s %.0f%% <10s %.0f%%"
					" 10s+ %.0f%% || owing POS: <1s %u <3s %u <10s %u 10s+ %u\n",
					100.0 * oS[0] / oTot, 100.0 * oS[1] / oTot, 100.0 * oS[2] / oTot,
					100.0 * oS[3] / oTot,
					oP[0] / (g_stats.frames ? g_stats.frames : 1),
					oP[1] / (g_stats.frames ? g_stats.frames : 1),
					oP[2] / (g_stats.frames ? g_stats.frames : 1),
					oP[3] / (g_stats.frames ? g_stats.frames : 1));
			}
		}

		unsigned int aSeen = 0, aLost = 0, aWide = 0, aFolded = 0, aLate = 0, aSetAfter = 0;
		AckLossStats(aSeen, aLost, aWide, aFolded, aLate, aSetAfter, true);
		const unsigned int unsentSkipped = g_unsentRequests;
		const unsigned int acksAhead = AcksAhead(true);
		g_unsentRequests = 0;
			if (aSeen > 0)
				snap_Say("[snap] datagram delivery: %u sent datagrams confirmed, %u LOST"
					" (%.2f%%), %u late, %u gaps beyond the window, %u folded on evidence"
					" | %u set after declared lost"
					" | %u requests for unsent packets skipped | %u acks ahead of sent\n",
					aSeen, aLost,
					100.0 * (double)aLost / (double)aSeen, aLate, aWide, aFolded, aSetAfter,
					unsentSkipped, acksAhead);
			else if ((unsentSkipped > 0) || (acksAhead > 0))
				snap_Say("[snap] datagram delivery: %u requests for unsent packets"
					" skipped | %u acks ahead of sent\n", unsentSkipped, acksAhead);

		// The reference check, while fua_snap_debug_refcheck runs it.
		{
			unsigned int rcF, rcFd, rcR, rcRd, rcP, rcPd;
			ReferenceCheckStats(rcF, rcFd, rcR, rcRd, rcP, rcPd, true);
			if ((rcF + rcR + rcP) > 0)
				snap_Say("[snap] reference check: %u in flight, %u DIFFER | %u at rest,"
					" %u DIFFER | %u psprites, %u DIFFER\n", rcF, rcFd, rcR, rcRd, rcP, rcPd);
		}

		{
			unsigned int mM = 0, mL = 0, mE = 0;
			MaybeStats(mM, mL, mE, true);
			if ((mM | mL | mE) != 0)
				snap_Say("[snap] unconfirmed enters: %u ids marked (lost without proof,"
					" not believed present) | %u leaves sent for them | %u cleared by a"
					" confirmed enter\n", mM, mL, mE);
		}
		{
			unsigned int rF = 0, rE = 0, rM = 0;
			RepairStats(rF, rE, rM, true);
			if (rF > 0)
				snap_Say("[snap] optimistic: repaired %u frames,"
					" %u entities, %u movers\n", rF, rE, rM);
		}

		// The marker count now, not over the window, staying up when an id
		// is sent an enter every frame.
		{
			unsigned int reenters = 0, onPresent = 0;
			PresenceStats(reenters, onPresent, true);
			snap_Say("[snap] presence: %u re-enters while a leave was in flight"
				" | %u leave markers on a present id\n", reenters, onPresent);
		}

		{
			unsigned int sent = 0;
			TargetStats(sent, true);
			snap_Say("[snap] missile target: world enters with one %u, spawner missiles with none %u"
				" | sent %.1f/frame, +%.0f B/frame\n",
				g_targetEnters, g_targetNoneEnters, (double)sent * inv, 2.0 * (double)sent * inv);
			g_targetEnters = g_targetNoneEnters = 0;
		}

		snap_Say("[snap] effects: %u captured actors carry effects (bits 0x%02x)\n",
			g_capEffectsActors, (unsigned int)g_capEffectsBits);
			g_capEffectsActors = 0;
			g_capEffectsBits = 0;

		snap_Say("[snap] priority: aging-forced %.0f%% | cosmetic held %.0f/frame | deferred %.0f/frame"
				" | entergrade %d\n",
				(emitted > 0) ? (100.0 * (double)g_forcedAccum * inv / emitted) : 0.0,
				(double)g_cosmeticAccum * inv, (double)g_deferredAccum * inv,
				(int)(bool)fua_snap_entergrade);

		// The quota the code used, not one derived again for the report.
		{
			unsigned int quota = 0, wanted = 0, pop = 0;
			AnimrefQuota(quota, wanted, pop);
			snap_Say("[snap] animref quota: %u/frame over %u held -> ~%.1f s"
					" | target %.1f s | ceiling %d%s\n",
					quota, pop,
					(quota > 0) ? ((double)pop / ((double)quota * pumpsPerSec)) : 0.0,
					(double)(int)fua_snap_animref_interval / (double)TICRATE,
					(int)SNAPCL_ANIMREF_MAX,
					(wanted > 0) ? " (CLAMPED)" : "");
		}

		// The encoding census, over records as written, not as captured.
		if (fua_snap_wirecensus)
		{
			SnapWireCensusOut wc;
			WireCensus(wc, true);
			QWORD recs = 0, bytes = 0, appOnly = 0, grpTotal = 0;
			for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
			{
				recs += wc.recs[t];
				bytes += wc.bytes[t];
				appOnly += wc.appOnly[t];
			}
			for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
				grpTotal += wc.grpBytes[g];

			if (recs > 0)
			{
				static const char* const szGroup[SNAP_GROUP_COUNT] =
					{ "pos", "posz", "ang", "velmisc", "appear", "render" };
				char sz[320];
				size_t at = 0;
				sz[0] = '\0';
				for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
				{
					const int w = mysnprintf(sz + at, sizeof(sz) - at, "%s%s %.0f%%/%.0f%%",
						(g > 0) ? " | " : "", szGroup[g],
						100.0 * (double)wc.grpRecs[g] / (double)recs,
						(grpTotal > 0) ? (100.0 * (double)wc.grpBytes[g] / (double)grpTotal) : 0.0);
					if ((w <= 0) || ((size_t)w >= sizeof(sz) - at))
						break;
					at += (size_t)w;
				}
				snap_Say("[snap] EMITTED groups (%% of records / %% of group bytes): %s\n", sz);
				snap_Say("[snap] EMITTED: %llu records, %.1f B mean"
					" | header %.0f%% of bytes | APPEAR-only %.0f%%"
					" | enters %.1f%%\n",
					(unsigned long long)recs, (double)bytes / (double)recs,
					100.0 * (double)wc.headerBytes / (double)bytes,
					100.0 * (double)appOnly / (double)recs,
					100.0 * (double)wc.enters / (double)recs);

				char szt[320];
				at = 0; szt[0] = '\0';
				for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				{
					if (wc.recs[t] == 0)
						continue;
					const int w = mysnprintf(szt + at, sizeof(szt) - at, "%s%s %.1fB/%.0f%%ap",
						(at > 0) ? " | " : "", g_szTierName[t],
						(double)wc.bytes[t] / (double)wc.recs[t],
						100.0 * (double)wc.appOnly[t] / (double)wc.recs[t]);
					if ((w <= 0) || ((size_t)w >= sizeof(szt) - at))
						break;
					at += (size_t)w;
				}
				snap_Say("[snap] EMITTED by band (mean record / APPEAR-only share): %s\n", szt);

				if (wc.idGapN > 0)
					snap_Say("[snap] sorted-id gaps, per FRAME: mean %.0f | 1-byte varint %.0f%%"
						" 2-byte %.0f%% 3-byte %.0f%% (%llu gaps)\n",
						(double)wc.idGapSum / (double)wc.idGapN,
						100.0 * (double)wc.idGap[0] / (double)wc.idGapN,
						100.0 * (double)wc.idGap[1] / (double)wc.idGapN,
						100.0 * (double)wc.idGap[2] / (double)wc.idGapN,
						(unsigned long long)wc.idGapN);

				// The figure an encoder takes: its base restarts at every chunk
				// cut, where it pays a raw id.
				if (wc.chunkGapN > 0)
				{
					// Against the raw 2 bytes: one saved per 1-byte gap, one
					// lost per 3-byte gap, chunk openers excluded.
					const double saved = (double)wc.chunkGap[0]
						- (double)wc.chunkGap[2];
					snap_Say("[snap] sorted-id gaps, per CHUNK: mean %.0f | 1-byte varint %.0f%%"
						" 2-byte %.0f%% 3-byte %.0f%% | %llu chunk-opening raw ids"
						" | net %.0f B/frame\n",
						(double)wc.chunkGapSum / (double)wc.chunkGapN,
						100.0 * (double)wc.chunkGap[0] / (double)wc.chunkGapN,
						100.0 * (double)wc.chunkGap[1] / (double)wc.chunkGapN,
						100.0 * (double)wc.chunkGap[2] / (double)wc.chunkGapN,
						(unsigned long long)wc.chunkFirst,
						(g_priorityFrames > 0) ? (saved / (double)g_priorityFrames) : 0.0);
				}

				// The JOINT over EVERY emitted APPEAR, the only figure a narrow form
				// can be sized from.
				if (wc.qsAppear > 0)
					snap_Say("[snap] APPEAR narrow form, JOINT over all %llu emitted:"
						" 8-bit offset %.2f%% | 7-bit offset %.2f%%"
						" | unresolvable %.2f%% (always wide) | %u client(s)\n",
						(unsigned long long)wc.qsAppear,
						100.0 * (double)wc.qsNarrow8 / (double)wc.qsAppear,
						100.0 * (double)wc.qsNarrow7 / (double)wc.qsAppear,
						100.0 * (double)wc.qsUnresolved / (double)wc.qsAppear,
						health.activeClients);
			}
		}
		}

		memset(g_tierAccum, 0, sizeof(g_tierAccum));
		g_forcedAccum = 0;
		g_cosmeticAccum = 0;
		g_deferredAccum = 0;
		g_priorityFrames = 0;
		g_sentBytes = 0;
		g_sentPackets = 0;
		g_wirePayloadBytes = 0;
		g_wireSentBytes = 0;
		g_wirePackets = 0;
		g_wireZstdPackets = 0;
		g_wirePeak = 0;
		g_chunkDatagrams = 0;
		g_chunkFrames = 0;
		g_freshAccum = 0;
		g_resendAccum = 0;
		g_resendByteAccum = 0;
		memset(g_tierSizeAccum, 0, sizeof(g_tierSizeAccum));
		g_resAllowAccum = g_resSpentAccum = 0;
		g_resCappedAccum = g_resDrainedAccum = 0;
		g_droppedPackets = 0;
		g_heldPackets = 0;
		if (g_enterByClass != NULL)
			memset(g_enterByClass, 0, SNAP_MAXID * sizeof(unsigned int));
		g_enterClassTotal = 0;
		ResetWindowStats();

		g_outboundAtWindowStart = SERVER_STATISTIC_GetTotalOutboundDataTransferred();
		g_haveOutboundMark = true;

		g_huntingAccum = 0;
		g_liveAccum = 0;
		g_ownedAccum = 0;
		g_huntingFrames = 0;
	}

	if (g_recordFile != NULL)
	{
		snap_Say("[snap] recording: %u records, %.1f MB\n",
			g_recordRecords, (double)g_recordBytes / (1024.0 * 1024.0));
		fflush(g_recordFile);	// bound data loss to one report window
	}

	g_stats.Reset();
	g_frameClamps = 0;
}

static void snap_SendResetEnd()
{
	g_snapResetCountSent = g_snapResetCount;
	SERVERCOMMANDS_DestroyAllSectorMovers();

	// After the destroy, not before: a client applies the count once nothing is left to
	// destroy the records it releases. Gated, so the legacy command's bytes stand alone.
	if (snap_GateOn())
		SERVERCOMMANDS_SnapMapReset(g_snapResetCount);
}

void zx::netsnapshot::ResetEnd()
{
	++g_snapResetCount;
	if ((fua_snap_debug_resetsplit > 0) && MoversOn())
	{
		g_resetSplitDue = gametic + fua_snap_debug_resetsplit;
		return;
	}
	// Sent now, it closes a deferred one as well.
	g_resetSplitDue = 0;
	snap_SendResetEnd();
}

BYTE zx::netsnapshot::MapStamp()
{
	return g_snapMapStamp;
}

BYTE zx::netsnapshot::MapResetCount()
{
	return g_snapResetCount;
}

BYTE zx::netsnapshot::MapResetCountSent()
{
	return g_snapResetCountSent;
}

void zx::netsnapshot::CountUnsentRequest()
{
	++g_unsentRequests;
}

// Legacy traffic by command (see CountLegacyCommand). [0] reliable,
// [1] unreliable; index svc, or 256 + svc2 for an extended command.
#define SNAP_LEGACY_KEYS	512
static QWORD g_legacyCmdBytes[2][SNAP_LEGACY_KEYS];
static QWORD g_legacyCmdCount[2][SNAP_LEGACY_KEYS];
static QWORD g_legacyFlushBytes = 0, g_legacyFlushes = 0;
static QWORD g_legacyResendBytes = 0, g_legacyResends = 0;
static QWORD g_legacySendRaw = 0;
// The snapshot's own chunks also go out as a NetCommand; kept apart, as a
// cross-check against the outbound line's snapshot figure.
static QWORD g_legacySnapBytes = 0;

void zx::netsnapshot::CountLegacyCommand(int svc, int svc2, unsigned int bytes, bool bReliable)
{
	if ((svc == SVC_EXTENDEDCOMMAND) && (svc2 == SVC2_SNAPSHOT))
	{
		g_legacySnapBytes += bytes;
		return;
	}
	const int key = (svc == SVC_EXTENDEDCOMMAND) ? (256 + (svc2 & 0xFF)) : (svc & 0xFF);
	g_legacyCmdBytes[bReliable ? 0 : 1][key] += bytes;
	++g_legacyCmdCount[bReliable ? 0 : 1][key];
}

void zx::netsnapshot::CountLegacyFlush(unsigned int bytes)
{
	g_legacyFlushBytes += bytes;
	++g_legacyFlushes;
}

void zx::netsnapshot::CountLegacyResend(unsigned int bytes)
{
	g_legacyResendBytes += bytes;
	++g_legacyResends;
}

void zx::netsnapshot::CountLegacySendRaw(QWORD raw)
{
	g_legacySendRaw += raw;
}

static const char * snap_LegacyKeyName(int key)
{
	return (key >= 256) ? GetStringSVC2(static_cast<SVC2>(key - 256))
		: GetStringSVC(static_cast<SVC>(key));
}

// The top `n` commands by bytes, on channel 0 (reliable), 1 (unreliable) or
// both (-1).
static FString snap_LegacyTop(int channel, int n, double toK)
{
	FString top;
	bool used[SNAP_LEGACY_KEYS];
	memset(used, 0, sizeof(used));
	for (int i = 0; i < n; ++i)
	{
		int best = -1;
		QWORD bestBytes = 0;
		for (int k = 0; k < SNAP_LEGACY_KEYS; ++k)
		{
			const QWORD b = (channel == 0) ? g_legacyCmdBytes[0][k]
				: (channel == 1) ? g_legacyCmdBytes[1][k]
				: (g_legacyCmdBytes[0][k] + g_legacyCmdBytes[1][k]);
			if ((used[k] == false) && (b > bestBytes))
			{
				best = k;
				bestBytes = b;
			}
		}
		if (best < 0)
			break;
		used[best] = true;
		const QWORD count = (channel == 0) ? g_legacyCmdCount[0][best]
			: (channel == 1) ? g_legacyCmdCount[1][best]
			: (g_legacyCmdCount[0][best] + g_legacyCmdCount[1][best]);
		top.AppendFormat("%s%s %.1f kB/s (%llu%s)", (i > 0) ? " | " : "",
			snap_LegacyKeyName(best), (double)bestBytes * toK, (unsigned long long)count,
			((channel < 0) && (g_legacyCmdBytes[1][best] > g_legacyCmdBytes[0][best]))
				? ", unreliable" : "");
	}
	return top;
}

// The outbound line's "everything else" by command, in raw bytes a second,
// compared with the compressed wire figure by share, not summed; `seconds`
// spans g_stats' game tics, so a skipped report averages.
static void snap_ReportLegacyTraffic(double seconds)
{
	if (seconds <= 0.0)
		return;
	QWORD cmdRel = 0, cmdUnrel = 0;
	for (int k = 0; k < SNAP_LEGACY_KEYS; ++k)
	{
		cmdRel += g_legacyCmdBytes[0][k];
		cmdUnrel += g_legacyCmdBytes[1][k];
	}
	const double toK = 1.0 / (1024.0 * seconds);
	if ((cmdRel | cmdUnrel | g_legacyFlushBytes | g_legacyResendBytes) != 0)
	{
		const double outside = (g_legacyFlushBytes > cmdRel)
			? (double)(g_legacyFlushBytes - cmdRel) : 0.0;
		snap_Say("[snap] legacy traffic (raw): reliable %.1f kB/s = commands %.1f + outside"
			" NetCommand %.1f + headers %.1f | RESENT %.1f kB/s (%llu packets) | unreliable"
			" commands %.1f kB/s | send %.2f ms/tic | snapshot chunks %.1f kB/s\n",
			((double)g_legacyFlushBytes + 5.0 * (double)g_legacyFlushes) * toK,
			(double)cmdRel * toK, outside * toK, 5.0 * (double)g_legacyFlushes * toK,
			(double)g_legacyResendBytes * toK, (unsigned long long)g_legacyResends,
			(double)cmdUnrel * toK,
			snap_RawToMS(g_legacySendRaw) / (seconds * (double)TICRATE),
			(double)g_legacySnapBytes * toK);

		// The top ten by bytes, either channel, and the reliable top five,
		// which is what the reclassify order reads.
		snap_Say("[snap] legacy top: %s\n", snap_LegacyTop(-1, 10, toK).GetChars());
		if (cmdRel > 0)
			snap_Say("[snap] legacy top reliable: %s\n", snap_LegacyTop(0, 5, toK).GetChars());
	}
	memset(g_legacyCmdBytes, 0, sizeof(g_legacyCmdBytes));
	memset(g_legacyCmdCount, 0, sizeof(g_legacyCmdCount));
	g_legacyFlushBytes = g_legacyFlushes = 0;
	g_legacyResendBytes = g_legacyResends = 0;
	g_legacySendRaw = 0;
	g_legacySnapBytes = 0;
}

//*****************************************************************************
//	LEGACY MOVER GATE (7.1)

// Latched once a pump, so every legacy mover command follows the SnapMoverGate
// that set it, not a cvar changed mid-tic.
static bool			g_moverGate = false;
static bool			g_moverCaptureWas = false;
static unsigned int	g_moverStopped[SNAPMV_COUNT];
static unsigned int	g_moverStoppedPlanes = 0;
static unsigned int	g_moverResent = 0;
static unsigned int	g_moverTableResets = 0;
// Ceiling and plat sounds sent, by the command that carried them.
static unsigned int	g_ceilSoundBySector = 0;
static unsigned int	g_ceilSoundLegacy = 0;
static unsigned int	g_platSoundBySector = 0;
static unsigned int	g_platSoundLegacy = 0;

// Deltas going out, as for SuppressesLegacy, at level 3 only.
static bool snap_MoverGateNow()
{
	return MoversOn() && fua_snap_send
		&& (fua_snap_nolegacy >= SNAPLEGACY_EXISTENCE);
}

bool zx::netsnapshot::GateOn()
{
	return snap_GateOn();
}

bool zx::netsnapshot::MoverGate()
{
	return g_moverGate;
}

void zx::netsnapshot::MoverSendDoorSound( sector_t * sec, int choice, bool bCheckSequence )
{
	if (( NETWORK_GetState() != NETSTATE_SERVER ) || ( g_moverGate == false ))
		return;
	SERVERCOMMANDS_DoorSound( sec, choice, bCheckSequence );
}

void zx::netsnapshot::MoverSendPlatSound( sector_t * sec, int platID, int type )
{
	if ( NETWORK_GetState() != NETSTATE_SERVER )
		return;
	// Gate off, stock's own command, so the bytes match. Every site broadcasts, and
	// DPlat::UpdateToClient sends no sound at all.
	if ( g_moverGate )
	{
		SERVERCOMMANDS_MoverSound( sec, SNAPSND_PACK( SNAPSND_PLAT, type ));
		++g_platSoundBySector;
	}
	else
	{
		SERVERCOMMANDS_PlayPlatSound( platID, type );
		++g_platSoundLegacy;
	}
}

void zx::netsnapshot::MoverSendCeilingSound( sector_t * sec, int ceilingID, int silent )
{
	if ( NETWORK_GetState() != NETSTATE_SERVER )
		return;
	// Gate off, stock's own command, so the bytes match. All four sites broadcast.
	if ( g_moverGate )
	{
		SERVERCOMMANDS_MoverSound( sec, SNAPSND_PACK( SNAPSND_CEILING, silent ));
		++g_ceilSoundBySector;
	}
	else
	{
		SERVERCOMMANDS_PlayCeilingSound( ceilingID );
		++g_ceilSoundLegacy;
	}
}

void zx::netsnapshot::MoverSoundStats( unsigned int & bySector, unsigned int & legacy,
	bool bReset )
{
	bySector = g_ceilSoundBySector + g_platSoundBySector;
	legacy = g_ceilSoundLegacy + g_platSoundLegacy;
	if ( bReset )
	{
		g_ceilSoundBySector = g_ceilSoundLegacy = 0;
		g_platSoundBySector = g_platSoundLegacy = 0;
	}
}

void zx::netsnapshot::MoverSoundStatsByKind( unsigned int * bySector, unsigned int * legacy,
	bool bReset )
{
	bySector[SNAPSND_CEILING] = g_ceilSoundBySector;
	bySector[SNAPSND_PLAT] = g_platSoundBySector;
	legacy[SNAPSND_CEILING] = g_ceilSoundLegacy;
	legacy[SNAPSND_PLAT] = g_platSoundLegacy;
	if ( bReset )
	{
		g_ceilSoundBySector = g_ceilSoundLegacy = 0;
		g_platSoundBySector = g_platSoundLegacy = 0;
	}
}

void zx::netsnapshot::MoverSendFloorSound( sector_t * sec, int floorID, unsigned int client,
	unsigned int flags )
{
	if ( NETWORK_GetState() != NETSTATE_SERVER )
		return;
	// Gate off, stock's send to every client, a join's resend included, so the bytes match.
	if ( g_moverGate )
		SERVERCOMMANDS_FloorSound( sec, client, ServerCommandFlags::FromInt( flags ));
	else
		SERVERCOMMANDS_StartFloorSound( floorID );
}

// A joiner is told the gate as it stands, but only while the snapshot is on: a gate-off
// server owes a joiner nothing, and the pump below sends the off itself, when GateOn has
// already gone false.
void zx::netsnapshot::SendMoverGateTo(unsigned int client)
{
	if (snap_GateOn() == false)
		return;
	SERVERCOMMANDS_SnapMoverGate(g_moverGate, 0, client, SVCF_ONLYTHISCLIENT);
}

bool zx::netsnapshot::MoverServerStops(BYTE cls, bool bPlane)
{
	if ((g_moverGate == false) || (MoverClassTracked(cls) == false))
		return false;
	++g_moverStopped[cls];
	if (bPlane)
		++g_moverStoppedPlanes;
	return true;
}

void zx::netsnapshot::ServerMoverGateStats(unsigned int * stopped, unsigned int & planes,
	unsigned int & resent, unsigned int & tableResets, bool bReset)
{
	memcpy(stopped, g_moverStopped, sizeof(g_moverStopped));
	planes = g_moverStoppedPlanes;
	resent = g_moverResent;
	tableResets = g_moverTableResets;
	if (bReset)
	{
		memset(g_moverStopped, 0, sizeof(g_moverStopped));
		g_moverStoppedPlanes = g_moverResent = g_moverTableResets = 0;
	}
}

static void snap_MoverGatePump()
{
	// Capture stopping clears the table, or its slots stay live with no
	// GONE to come.
	const bool bCapture = MoversOn();
	if (g_moverCaptureWas && (bCapture == false))
	{
		MoverReset();
		ForgetMovers();
		++g_moverTableResets;
	}
	g_moverCaptureWas = bCapture;

	const bool bGate = snap_MoverGateNow();
	if (bGate == g_moverGate)
		return;
	g_moverGate = bGate;

	// Either way the client drops one side's movers, so every live record
	// goes again, or the re-sent legacy ones are adopted below level 3.
	ForgetMovers();
	if (bGate)
	{
		SERVERCOMMANDS_SnapMoverGate(true, 0);
		return;
	}

	// From the next frame built, then every live tracked mover as legacy,
	// since records have stopped.
	SERVERCOMMANDS_SnapMoverGate(false, (int)(g_snapFrame + 1));
	for (ULONG i = 0; i < MAXPLAYERS; ++i)
	{
		if (SERVER_IsValidClient(i) && (SERVER_GetClient(i)->State == CLS_SPAWNED))
			g_moverResent += SERVER_ResendTrackedMovers(i);
	}
}

//*****************************************************************************
//	ENTRY POINT - called at the end of every SERVER_Tick.

void zx::netsnapshot::ServerTick()
{
	// First, so the rest of this tick acts on the gate as it stands now.
	snap_GateTick();

	// Above the fua_snap test, so turning it off mid-delay cannot strand
	// the send and leave every client's window open.
	if ((g_resetSplitDue != 0) && (gametic >= g_resetSplitDue))
	{
		g_resetSplitDue = 0;
		snap_SendResetEnd();
	}

	// Above it for the same reason: fua_snap off must clear the gate.
	if (NETWORK_GetState() == NETSTATE_SERVER)
		snap_MoverGatePump();

	if ((NETWORK_GetState() != NETSTATE_SERVER) || (snap_GateOn() == false))
		return;

	// The wall period since the previous pump, sampled before any work, the
	// catch-up's cause measured directly.
	const tictime_raw_t pumpNow = TIC_GetRawCounter();
	if (g_lastPumpRaw != 0)
	{
		const QWORD gapRaw = (QWORD)(pumpNow - g_lastPumpRaw);
		g_pumpGapSumRaw += gapRaw;
		++g_pumpGapCount;
		if (gapRaw > g_pumpGapWorstRaw)
		{
			g_pumpGapWorstRaw = gapRaw;
			g_pumpGapWorstTics = gametic - g_lastPumpGametic;
		}
	}
	g_lastPumpRaw = pumpNow;
	g_lastPumpGametic = gametic;

	snap_CapturePSprites();

	// The whole snapshot tick, the per-client pass timed inside it and the
	// capture the remainder.
	const tictime_raw_t tickStart = TIC_GetRawCounter();
	const QWORD sendBefore = g_phaseSendRaw;

	snap_Init();
	ServerInit();
	snap_ReleaseDelayedAcks();

	// Before the capture: a slot released this tic must not still be occupied
	// when a thinker at the same address captures into it.
	MoverTick();

	snap_CaptureAndMeasure();

	const QWORD tickRaw = (QWORD)(TIC_GetRawCounter() - tickStart);
	g_phaseTickRaw += tickRaw;
	g_phaseCaptureRaw += tickRaw - (g_phaseSendRaw - sendBefore);
	if (tickRaw > g_phaseWorstRaw)
		g_phaseWorstRaw = tickRaw;
	++g_phaseFrames;

	// After the accumulate, timed from outside, so the cost includes printing.
	const tictime_raw_t repStart = TIC_GetRawCounter();
	snap_MaybeReport();
	const QWORD repRaw = (QWORD)(TIC_GetRawCounter() - repStart);
	if (repRaw > g_reportRaw)
		g_reportRaw = repRaw;

	// One report line a pump, after the timing, which so leaves out the drain.
	if (g_reportQueue.Size() > 0)
	{
		Printf("%s", g_reportQueue[0].GetChars());
		g_reportQueue.Delete(0);
	}
}

//*****************************************************************************
//	RECORDING COMMANDS

CCMD(fua_snap_record)
{
	if (NETWORK_GetState() != NETSTATE_SERVER)
	{
		Printf("fua_snap_record is only available on a server.\n");
		return;
	}
	if (g_recordFile != NULL)
	{
		Printf("Already recording. Use fua_snap_stoprecord first.\n");
		return;
	}
	// Loading a map ends a recording, so one started before a map is up
	// would hold nothing but its header.
	if (gamestate != GS_LEVEL)
	{
		Printf("fua_snap_record: no map is loaded; start the recording once one is.\n");
		return;
	}
	if (argv.argc() < 2)
	{
		Printf("Usage: fua_snap_record <filename>\n");
		return;
	}

	// Keep the file in the working directory: reject path separators.
	const char* pszName = argv[1];
	for (const char* p = pszName; *p; ++p)
	{
		if ((*p == '/') || (*p == '\\') || (*p == ':'))
		{
			Printf("fua_snap_record: filename must not contain path separators.\n");
			return;
		}
	}

	FString filename = pszName;
	if (filename.IndexOf('.') == -1)
		filename += ".zsnap";

	g_recordFile = fopen(filename.GetChars(), "wb");
	if (g_recordFile == NULL)
	{
		Printf("fua_snap_record: couldn't open '%s' for writing.\n", filename.GetChars());
		return;
	}

	// File header.
	fwrite("ZSNP", 1, 4, g_recordFile);
	snap_RecordWrite16(ZSNAP_VERSION);
	snap_RecordWrite16(TICRATE);
	snap_RecordWrite32(ZSNAP_FLAG_ZSTD);
	snap_RecordWrite32(0);
	// The frame reads every payload in the file, which holds one map.
	const SnapFrame &frame = ServerMapFrame();
	for (unsigned int k = 0; k < 3; ++k)
		snap_RecordWrite32((DWORD)frame.origin[k]);
	for (unsigned int k = 0; k < 3; ++k)
		fwrite(&frame.width[k], 1, 1, g_recordFile);

	g_recordRecords = 0;
	g_recordBytes = ZsnapHeaderBytes(ZSNAP_VERSION);

	// The next capture emits a full baseline, a one-off blip in the
	// window's enters.
	if (g_snapInitialized)
		memset(g_prevPresent, 0, SNAP_MAXID);

	if (snap_GateOn() == false)
		Printf("fua_snap_record: note - fua_snap is OFF; nothing will be captured until it is enabled.\n");

	Printf("fua_snap_record: recording to '%s'.\n", filename.GetChars());
}

CCMD(fua_snap_stoprecord)
{
	if (g_recordFile == NULL)
	{
		Printf("Not currently recording.\n");
		return;
	}
	snap_RecordStop("stopped by console");
}

//*****************************************************************************
//	EFFECT BITS

// Fails the build if the engine's effect bits move from format.h's.
typedef char snap_AssertEffectBits[
((SNAP_FX_ROCKET == FX_ROCKET)
	&& (SNAP_FX_GRENADE == FX_GRENADE)
	&& (SNAP_FX_RESPAWNINVUL == FX_RESPAWNINVUL)
	&& (SNAP_FX_VISIBILITYPULSE == FX_VISIBILITYPULSE)
	&& (SNAP_FX_VISIBILITYFLICKER == FX_VISIBILITYFLICKER)
	&& (SNAP_FX_FOUNTAINMASK == FX_FOUNTAINMASK)
	&& (SNAP_FX_FOUNTAINSHIFT == FX_FOUNTAINSHIFT)) ? 1 : -1];

//*****************************************************************************
//	PLANE WRITE TRACE
//
// Every plane write from outside a mover thinker, linked sectors' too, by
// sector index (sv_snapshot.md).
CVAR(Int, fua_csnap_planetrace, 0, CVAR_NOSETBYACS)	// sector index, -1 all, 0 off

// Report of the sector link graph, taken by the join sector pass.
CVAR(Bool, fua_snap_linkcensus, false, CVAR_NOSETBYACS)

// Orders writes within a tic, where two to one sector are the fault.
static unsigned int g_planeWriteSeq = 0;
static bool g_planeTraceWarned = false;

void zx::netsnapshot::TracePlaneWrite(sector_t * pSec, int plane, const char * pszWho,
	fixed_t before, fixed_t after, const sector_t * pFrom)
{
	if (fua_csnap_planetrace == 0)
		return;

	// Client only, with a warning, since a Printf per write would back the
	// server up.
	if (NETWORK_InClientMode() == false)
	{
		if (g_planeTraceWarned == false)
		{
			g_planeTraceWarned = true;
			Printf("fua_csnap_planetrace: client only, ignored here\n");
		}
		return;
	}

	if (pSec == NULL)
		return;
	const int idx = (int)(pSec - sectors);
	if ((fua_csnap_planetrace >= 0) && (idx != (int)fua_csnap_planetrace))
		return;

	// Empty when the write was addressed to this sector rather than propagated.
	char from[32];
	from[0] = '\0';
	if (pFrom != NULL)
		mysnprintf(from, sizeof(from), " from sec %d", (int)(pFrom - sectors));

	const bool bCeil = (plane == sector_t::ceiling);
	const bool bThinker = bCeil ? (pSec->ceilingdata != NULL)
		: (pSec->floordata != NULL);

	// Every write, its delta signed so the propagation reads directly.
	Printf("[plane] #%u sec %d tag %d %s WRITE by %s%s: %d -> %d (delta %d)"
		" | texz+d %d vbo %d %s | thinker %s | tic %d\n",
		++g_planeWriteSeq, idx, tagManager.GetFirstSectorTag(pSec), bCeil ? "ceil" : "floor",
		pszWho, from, (int)before, (int)after, (int)(after - before),
		(int)(pSec->GetPlaneTexZ(plane) + pSec->GetSecPlane(plane).d),
		(int)pSec->vboheight[plane],
		pSec->VBOHeightcheck(plane) ? "VALID" : "stale",
		bThinker ? "live" : "none", gametic);
}

//*****************************************************************************
//	WHOLE-MAP PLANE DUMP
//
// Dumps sector planes by index to diff across machines (sv_snapshot.md): every
// mover or tagged sector, `all`, or one index and its linked sectors.
CCMD(fua_snap_planedump)
{
	if (numsectors <= 0)
	{
		Printf("fua_snap_planedump: no level loaded\n");
		return;
	}

	const bool bAll = (argv.argc() > 1) && (stricmp(argv[1], "all") == 0);
	int only = -1;
	if ((argv.argc() > 1) && (bAll == false))
		only = atoi(argv[1]);

	Printf("fua_snap_planedump: %d sectors, gametic %d, %s\n", numsectors, gametic,
		(NETWORK_GetState() == NETSTATE_SERVER) ? "SERVER" : "client");
	Printf("  idx  tag       floor.d      texz+d   ceil.d  thinker  vbo\n");

	unsigned int shown = 0;
	for (int i = 0; i < numsectors; ++i)
	{
		sector_t * const pSec = &sectors[i];

		if (only >= 0)
		{
			if (i != only)
				continue;
		}
		else if (bAll == false)
		{
			// Movers and tagged sectors, the tag reaching a linked lift's
			// visible sectors.
			// [Kaori-Chani] A sector carries a list of tags here, not the one short it did in the
			// source, so "untagged" asks the tag manager rather than comparing a first tag with
			// zero: a sector tagged 0 and a sector with no tag at all are different things to it.
			if ((pSec->floordata == NULL) && (pSec->ceilingdata == NULL)
				&& (tagManager.SectorHasTags(pSec) == false))
				continue;
		}

		// vbo is the renderer's cached plane: a server never draws, so it has
		// none, and prints - rather than a value that diffs against clients.
		char vbo[32];
		if (NETWORK_GetState() == NETSTATE_SERVER)
			mysnprintf(vbo, sizeof(vbo), "-");
		else
			mysnprintf(vbo, sizeof(vbo), "%d %s",
				(int)pSec->vboheight[sector_t::floor],
				pSec->VBOHeightcheck(sector_t::floor) ? "VALID" : "stale");
		Printf("  %4d %4d %12d %11d %8d  %-7s  %s\n",
			i, tagManager.GetFirstSectorTag(pSec), (int)pSec->floorplane.d,
			(int)(pSec->GetPlaneTexZ(sector_t::floor) + pSec->floorplane.d),
			(int)pSec->ceilingplane.d,
			(pSec->floordata != NULL) ? "floor"
				: ((pSec->ceilingdata != NULL) ? "ceil" : "-"),
			vbo);
		for (int p = 0; (p < 2) && (pSec->e != NULL); ++p)
		{
			const TArray<line_t *> &mid = p ? pSec->e->Midtex.Ceiling.AttachedLines
				: pSec->e->Midtex.Floor.AttachedLines;
			if (mid.Size() == 0)
				continue;
			Printf("       midtex %s: line %d yoff %d (%u lines)\n",
				p ? "ceil" : "floor", (int)(mid[0] - lines),
				(int)mid[0]->sidedef[0]->GetTextureYOffset(side_t::mid),
				mid.Size());
		}
		++shown;
	}
	// Polyobjects by StartSpot and angle, ahead of the closing line.
	for (int i = 0; i < po_NumPolyobjs; ++i)
	{
		const FPolyObj * const po = &polyobjs[i];
		int actions = 0;
		for (const DPolyAction * a = po->snapActions; a != NULL; a = a->m_SnapNext)
			++actions;
		Printf("  poly %4d %4d %12d %12d %10u  actions %d%s\n", i, po->tag,
			(int)po->StartSpot.x, (int)po->StartSpot.y, (unsigned int)po->angle,
			actions, (po->specialdata != NULL) ? " special" : "");
	}
	// Scrollers and pushers with their control sector, velocity and source.
	{
		TThinkerIterator<DScroller> it;
		DScroller * p;
		while ((p = it.Next()) != NULL)
			if (bAll || p->IsAccelerative() || p->HasControl())
				p->SnapDump();
	}
	{
		TThinkerIterator<DPusher> it;
		DPusher * p;
		while ((p = it.Next()) != NULL)
			if (bAll || p->IsPoint())
				p->SnapDump();
	}
	Printf("fua_snap_planedump: %u sectors shown\n", shown);
}

//*****************************************************************************
//
// Prints the mover sound counts and zeroes them, so a window can align the counter with its
// capture: called at fua_netcap_start, the next read covers exactly the captured interval.
CCMD(fua_snap_soundcount)
{
	unsigned int bySector[2] = { 0, 0 }, legacy[2] = { 0, 0 };
	zx::netsnapshot::MoverSoundStatsByKind(bySector, legacy, true);
	Printf("[snap] mover sounds since the last read: ceiling %u by sector, %u legacy"
		" | plat %u by sector, %u legacy\n",
		bySector[SNAPSND_CEILING], legacy[SNAPSND_CEILING],
		bySector[SNAPSND_PLAT], legacy[SNAPSND_PLAT]);
}

//*****************************************************************************
//
CCMD(fua_snap_sectorcheck)
{
	if (argv.argc() < 2)
	{
		Printf("usage: fua_snap_sectorcheck <tag>\n");
		return;
	}
	const int tag = atoi(argv[1]);
	int secnum = -1;
	unsigned int found = 0;

	if (NETWORK_InClientMode())
		Printf("[client] gametic %d, newest server tic %d\n",
			gametic, NewestGameTic());
	else
		Printf("[server] gametic %d\n", gametic);
	// [Kaori-Chani] P_FindSectorFromTag's walk is the tag manager's iterator here, since a sector
	// holds a list of tags rather than one: cl_main.cpp's own colour and light commands walk it
	// this way, so this reads as the engine's other tag loops do.
	FSectorTagIterator sectorsWithTag(tag);
	while ((secnum = sectorsWithTag.Next()) >= 0)
	{
		++found;
		// NOT const: TObjPtr's conversion to a raw pointer is non-const, so a
		// const sector cannot yield its floordata at all.
		sector_t * const pSec = &sectors[secnum];
		Printf("sector %d (tag %d): floor.d %d, ceiling.d %d\n",
			secnum, tag, (int)pSec->floorplane.d, (int)pSec->ceilingplane.d);
		// A mover's sound, readable without a sound device: the node exists whenever the
		// sequence was started, so an absent one is a sound that never played.
		Printf("  sequence: floor %s, ceiling %s\n",
			(SN_CheckSequence(pSec, CHAN_FLOOR) != NULL) ? "playing" : "none",
			(SN_CheckSequence(pSec, CHAN_CEILING) != NULL) ? "playing" : "none");

		DSectorEffect * const pFloor = pSec->floordata;
		DSectorEffect * const pCeil = pSec->ceilingdata;
		for (unsigned int k = 0; k < 2; ++k)
		{
			DSectorEffect * const p = (k == 0) ? pFloor : pCeil;
			if (p == NULL)
				continue;
			Printf("  %s: %s\n", (k == 0) ? "floordata" : "ceilingdata",
				p->GetClass()->TypeName.GetChars());
			if (p->IsKindOf(RUNTIME_CLASS(DPlat)))
			{
				DPlat * const pl = static_cast<DPlat *>(p);
				// The cycle position, uninitialized on a client's legacy-made
				// plat (a count in the millions is that, not a phase).
				Printf("    status %d count %d wait %d low %d high %d\n",
					(int)pl->GetStatus(), pl->GetCount(), pl->GetWait(),
					(int)pl->GetLow(), (int)pl->GetHigh());
			}
		}
	}
	if (found == 0)
		Printf("no sector with tag %d\n", tag);
}

//*****************************************************************************
//	MOVER CAPTURE
//
// The capture compares before it bumps a slot's seq, leaving out the plane,
// which rides every record (design doc 7.1).

// `seq` only ever increases and does double duty: the dirty test, and the
// generation that makes slot reuse safe.
namespace {
struct SnapMoverSlot
{
	const void *	owner;		// thinker identity; NULL = free
	WORD			seq;
	WORD			index;		// sector index, or polyobjs[] index for SNAPMV_POLY
	BYTE			cls;		// Mover*
	BYTE			flags;		// SNAPMVF_*
	BYTE			gen;		// instance generation, for the envelope
	BYTE			cut;		// planes a takeover narrowed off this GONE
	int				goneTic;	// gametic the GONE flag was set, 0 = live
	unsigned int	goneFrame;	// g_moverFrame at that capture; the sweep's clock
	// One slot is one class, `cls` selecting the body each capture compares.
	union
	{
		SnapMoverPlat		plat;
		SnapMoverCeiling	ceiling;
		SnapMoverFloor		floor;
		SnapMoverDoor		door;
		SnapMoverElevator	elevator;
		SnapMoverPillar		pillar;
	} body;
	// SNAPMV_POLY's body is g_moverPolyBody[index]: one slot per polyobject, and
	// 11 entries would grow every slot tenfold.
};
}

// SNAPMV_MAXSLOTS is in capture.h; the per-client pass sizes arrays from it.

// The backstop, in frames, on a gone slot waiting only for clients that never
// acked, which fua_snap_debug_ackdelay must stay well below.
#define SNAPMV_GONE_HOLD_FRAMES	(SNAPCL_MAX_PENDING * 2)

static SnapMoverSlot	g_moverSlots[SNAPMV_MAXSLOTS];
static unsigned int		g_moverSlotsUsed = 0;	// high-water mark, walked to
static unsigned int		g_moverSlotsFull = 0;	// captures refused, no slot
static unsigned int		g_moverCaptures = 0;	// samples taken this window
static unsigned int		g_moverBumps = 0;		// of those, ones that changed
static unsigned int		g_moverBlocked = 0;		// of those, ones with SNAPMVF_BLOCKED
// Gone slots a live mover took over, by recycled address or plane alone, and
// `disowned`, a recycled address on another plane.
static unsigned int		g_moverRecycled = 0;
static unsigned int		g_moverRecycledPlane = 0;
static unsigned int		g_moverDisowned = 0;
// Gone slots a takeover released at once, and those it narrowed to the planes
// the new mover does not cover.
static unsigned int		g_moverRetired = 0;
static unsigned int		g_moverNarrowed = 0;
// A disowned slot's owner, matching no thinker, so only the sweep finds it.
static const char		s_moverDisowned = 0;
// The sweep's clock: one per MoverTick, i.e. per frame, not per gametic.
static unsigned int		g_moverFrame = 0;
// Gone slots released with a client unacked: `unstarted` by the backstop, and
// `pressure` taken for a live mover from a full table.
static unsigned int		g_moverSweptUnstarted = 0;
static unsigned int		g_moverSweptPressure = 0;
// Gone slots a started client held past the old backstop (until acked or gone).
static unsigned int		g_moverHeldPast = 0;
static unsigned int		g_moverHeldLongest = 0;	// frames, over the window

// SNAPMV_POLY bodies by polyobject index, zeroed on growth and at a map change.
static TArray<SnapMoverPoly>	g_moverPolyBody;
// Polyobject slots a live capture reclaimed, and entries left out over cap.
static unsigned int		g_moverPolyReclaimed = 0;
static unsigned int		g_moverPolyOverCap = 0;
// Entries each polyobject's last capture left out, so a drop counts once.
static TArray<BYTE>		g_moverPolyOverLast;
// A perpetual rotator's period between re-emitted records, each polyobject on
// its own phase.
#define SNAPMV_POLY_RESYNC_TICS	175
static TArray<int>		g_moverPolyResyncTic;
static unsigned int		g_moverPolyResyncs = 0;

// Mover capture on the server, archived and on, under fua_snap.
CVAR(Bool, fua_snap_movers, true, CVAR_ARCHIVE | CVAR_NOSETBYACS)

// One generation counter for every slot, skipping 0, which means none.
static BYTE				g_moverGen = 0;

static BYTE snap_NextMoverGen()
{
	if (++g_moverGen == 0)
		g_moverGen = 1;
	return g_moverGen;
}

BYTE zx::netsnapshot::NewMoverGen()
{
	return snap_NextMoverGen();
}

// Hands a gone slot to a new live mover, carrying its seq over (so the new
// record supersedes the pending GONE) but not its generation.
static SnapMoverSlot * snap_MoverReclaim(SnapMoverSlot & sl, const void * pOwner)
{
	const WORD keep = sl.seq;
	memset(&sl, 0, sizeof(sl));
	sl.seq = keep;
	sl.owner = pOwner;
	sl.gen = snap_NextMoverGen();
	return &sl;
}

// The planes a slot's record still speaks for.
static BYTE snap_SlotPlanes(const SnapMoverSlot & sl)
{
	return (BYTE)(MoverPlanes(sl.cls) & ~sl.cut);
}

// Whether an in-map client has yet to ack gone slot i, and whether one of
// those has ever acked anything.
static bool snap_GoneBehind(unsigned int i, bool & bStarted)
{
	bool bBehind = false;
	bStarted = false;
	for (unsigned int c = 0; c < MAXPLAYERS; ++c)
	{
		if (snap_ClientInMap(c)
			&& (MoverAcked(c, i, g_moverSlots[i].seq) == false))
		{
			bBehind = true;
			if (StreamStarted(c))
			{
				bStarted = true;
				break;
			}
		}
	}
	return bBehind;
}

// The slot for a thinker pointer (not a sector), a live capture landing on a
// gone slot being a recycled address and so a different mover.
static SnapMoverSlot * snap_MoverSlotFor(const void * pOwner, bool bAllocate,
	BYTE cls, WORD index)
{
	unsigned int free = SNAPMV_MAXSLOTS;
	unsigned int own = SNAPMV_MAXSLOTS;
	for (unsigned int i = 0; i < g_moverSlotsUsed; ++i)
	{
		SnapMoverSlot & sl = g_moverSlots[i];
		if (sl.owner == pOwner)
		{
			// A gone capture keeps its own slot, but a live one here is a
			// recycled thinker address.
			if ((bAllocate == false) || (sl.goneTic == 0))
				return &sl;
			// A polyobject owns its slot for the map, so it is reclaimed in
			// place, its seq carrying over past the GONE.
			if (cls == SNAPMV_POLY)
			{
				++g_moverPolyReclaimed;
				return snap_MoverReclaim(sl, pOwner);
			}
			own = i;
			continue;
		}
		if ((sl.owner == NULL) && (free == SNAPMV_MAXSLOTS))
			free = i;
	}
	if (bAllocate == false)
		return NULL;

	// Takeover by plane set (7.1): the first covered predecessor is reclaimed,
	// others retired, and one with a plane this leaves narrowed.
	const BYTE planes = MoverPlanes(cls);
	SnapMoverSlot * pReclaim = NULL;
	if (own != SNAPMV_MAXSLOTS)
	{
		SnapMoverSlot & sl = g_moverSlots[own];
		const BYTE held = snap_SlotPlanes(sl);
		if ((sl.index == index) && (planes != 0) && ((held & planes) != 0)
			&& ((held & ~planes) == 0))
		{
			pReclaim = &sl;
			++g_moverRecycled;
		}
	}
	for (unsigned int i = 0; i < g_moverSlotsUsed; ++i)
	{
		SnapMoverSlot & sl = g_moverSlots[i];
		if ((&sl == pReclaim) || (sl.owner == NULL) || (sl.goneTic == 0)
			|| (sl.index != index))
			continue;
		const BYTE held = snap_SlotPlanes(sl);
		if ((held & planes) == 0)
			continue;
		if (held & ~planes)
		{
			sl.cut |= (BYTE)(held & planes);
			++g_moverNarrowed;
		}
		else if (pReclaim == NULL)
		{
			pReclaim = &sl;
			++g_moverRecycledPlane;
		}
		else
		{
			if (i == own)
				own = SNAPMV_MAXSLOTS;
			sl.owner = NULL;
			sl.goneTic = 0;
			++g_moverRetired;
		}
	}

	// A recycled address not reclaimed keeps its GONE for the planes it still
	// holds: disowned to the sweep, which re-sends it and releases it on the acks.
	if ((own != SNAPMV_MAXSLOTS) && (pReclaim != &g_moverSlots[own]))
	{
		g_moverSlots[own].owner = &s_moverDisowned;
		++g_moverDisowned;
	}
	if (pReclaim != NULL)
		return snap_MoverReclaim(*pReclaim, pOwner);

	if ((free == SNAPMV_MAXSLOTS) && (g_moverSlotsUsed < SNAPMV_MAXSLOTS))
		free = g_moverSlotsUsed++;
	// A full table: the oldest gone slot goes, so a client slow to ack can
	// cost itself a record but never cost every client a live mover.
	if (free == SNAPMV_MAXSLOTS)
	{
		unsigned int oldestAge = 0;
		for (unsigned int i = 0; i < g_moverSlotsUsed; ++i)
		{
			const SnapMoverSlot & sl = g_moverSlots[i];
			if ((sl.owner == NULL) || (sl.goneTic == 0))
				continue;
			const unsigned int age = g_moverFrame - sl.goneFrame;
			if ((free == SNAPMV_MAXSLOTS) || (age > oldestAge))
			{
				free = i;
				oldestAge = age;
			}
		}
		// Only one a started client still owes counts: an ack can land after
		// this pump's sweep, and a client that never started holds nothing.
		bool bStarted = false;
		if ((free != SNAPMV_MAXSLOTS) && snap_GoneBehind(free, bStarted) && bStarted)
			++g_moverSweptPressure;
	}
	if (free == SNAPMV_MAXSLOTS)
	{
		++g_moverSlotsFull;
		return NULL;
	}
	SnapMoverSlot & sl = g_moverSlots[free];
	// NOT reset: restarting it would let a client's stale value for the
	// previous occupant suppress the new one's record.
	const WORD keep = sl.seq;
	memset(&sl, 0, sizeof(sl));
	sl.seq = keep;
	sl.owner = pOwner;
	sl.gen = snap_NextMoverGen();
	return &sl;
}

// Releases a gone slot once every started, in-map client has acked its GONE,
// clients that never acked falling to the backstop (design doc 7.1).
static void snap_MoverSweepGone()
{
	for (unsigned int i = 0; i < g_moverSlotsUsed; ++i)
	{
		SnapMoverSlot & sl = g_moverSlots[i];
		if ((sl.owner == NULL) || (sl.goneTic == 0))
			continue;

		bool bStartedBehind = false;
		const bool bBehind = snap_GoneBehind(i, bStartedBehind);
		const unsigned int age = g_moverFrame - sl.goneFrame;
		if (bStartedBehind)
		{
			if ((age >= SNAPMV_GONE_HOLD_FRAMES) && (age > g_moverHeldLongest))
				g_moverHeldLongest = age;
			continue;
		}
		if (bBehind)
		{
			if (age < SNAPMV_GONE_HOLD_FRAMES)
				continue;
			++g_moverSweptUnstarted;
		}
		else if (age >= SNAPMV_GONE_HOLD_FRAMES)
			++g_moverHeldPast;
		sl.owner = NULL;
		sl.goneTic = 0;
	}
}

// Once per pump, which is what makes g_moverFrame a frame count; capture is
// driven by the thinkers.
void zx::netsnapshot::MoverTick()
{
	++g_moverFrame;
	snap_MoverSweepGone();
}

// The capture's gate, shared with the door sound so the two cannot drift.
// fua_snap too: records only ride snapshot frames and only the snapshot tick
// sweeps, so a capture without it fills slots nothing ever releases.
bool zx::netsnapshot::MoversOn()
{
	return fua_snap_movers && snap_GateOn() && (NETWORK_GetState() == NETSTATE_SERVER);
}

// The half every class shares, the seq and gone bookkeeping; `bAllocate` false
// finds an existing slot only, as a GONE does.
static SnapMoverSlot * snap_MoverBegin(const void * pOwner, BYTE cls,
	WORD index, bool bBlocked, bool bGone, BYTE & flags, bool bAllocate = true)
{
	if (MoversOn() == false)
		return NULL;

	SnapMoverSlot * pSlot = snap_MoverSlotFor(pOwner, bAllocate && (bGone == false),
		cls, index);
	if (pSlot == NULL)
		return NULL;			// gone and never captured, or the table is full
	++g_moverCaptures;

	flags = (BYTE)((bBlocked ? SNAPMVF_BLOCKED : 0)
		| (bGone ? SNAPMVF_GONE : 0));

	// Counted, so a server that never blocks and a flag that never reaches the
	// wire read differently from a log.
	if (bBlocked)
		++g_moverBlocked;

	return pSlot;
}

static void snap_MoverCommit(SnapMoverSlot * pSlot, BYTE cls, WORD index,
	BYTE flags, bool bGone, bool bChanged)
{
	pSlot->cls = cls;
	pSlot->index = index;
	pSlot->flags = flags;
	if (bGone)
	{
		pSlot->goneTic = (gametic != 0) ? gametic : 1;
		pSlot->goneFrame = g_moverFrame;
	}

	if (bChanged)
	{
		// Wraps at 65,536 per slot, and the test is equality, so a wrap costs
		// one suppressed record.
		++pSlot->seq;
		if (pSlot->seq == 0)
			pSlot->seq = 1;		// 0 is "this client has been sent nothing"
		++g_moverBumps;
	}
}

void zx::netsnapshot::MoverCapturePlat(const void * pOwner, WORD index,
	const SnapMoverPlat & s, bool bBlocked, bool bGone)
{
	BYTE flags = 0;
	SnapMoverSlot * pSlot = snap_MoverBegin(pOwner, SNAPMV_PLAT, index,
		bBlocked, bGone, flags);
	if (pSlot == NULL)
		return;

	// Everything but the plane and m_Count is compared, flags included,
	// and both ride the record.
	SnapMoverPlat cmp = pSlot->body.plat;
	cmp.planeD = s.planeD;
	cmp.count = s.count;
	const bool bChanged = (pSlot->cls != SNAPMV_PLAT)
		|| (pSlot->index != index)
		|| (pSlot->flags != flags)
		|| (memcmp(&cmp, &s, sizeof(cmp)) != 0);

	pSlot->body.plat = s;
	snap_MoverCommit(pSlot, SNAPMV_PLAT, index, flags, bGone, bChanged);
}

// DFloor's compare leaves out the plane and the reset, pause and step
// countdowns, which still ride the record.
void zx::netsnapshot::MoverCaptureFloor(const void * pOwner, WORD index,
	const SnapMoverFloor & s, bool bStair, bool bBlocked, bool bGone)
{
	BYTE flags = 0;
	SnapMoverSlot * pSlot = snap_MoverBegin(pOwner, SNAPMV_FLOOR, index,
		bBlocked, bGone, flags);
	if (pSlot == NULL)
		return;
	if (bStair)
		flags |= SNAPMVF_STAIR;

	SnapMoverFloor cmp = pSlot->body.floor;
	cmp.planeD = s.planeD;
	cmp.resetCount = s.resetCount;
	cmp.pauseTime = s.pauseTime;
	cmp.stepTime = s.stepTime;
	const bool bChanged = (pSlot->cls != SNAPMV_FLOOR)
		|| (pSlot->index != index)
		|| (pSlot->flags != flags)
		|| (memcmp(&cmp, &s, sizeof(cmp)) != 0);

	pSlot->body.floor = s;
	snap_MoverCommit(pSlot, SNAPMV_FLOOR, index, flags, bGone, bChanged);
}

// DCeiling has no wait, so the plane is the only phase and the only exclusion.
// m_Speed IS compared: a crusher changes it at every reversal and again when it
// meets an obstruction, which is a transition the client must be told about.
void zx::netsnapshot::MoverCaptureCeiling(const void * pOwner, WORD index,
	const SnapMoverCeiling & s, bool bBlocked, bool bGone)
{
	BYTE flags = 0;
	SnapMoverSlot * pSlot = snap_MoverBegin(pOwner, SNAPMV_CEILING, index,
		bBlocked, bGone, flags);
	if (pSlot == NULL)
		return;

	SnapMoverCeiling cmp = pSlot->body.ceiling;
	cmp.planeD = s.planeD;
	const bool bChanged = (pSlot->cls != SNAPMV_CEILING)
		|| (pSlot->index != index)
		|| (pSlot->flags != flags)
		|| (memcmp(&cmp, &s, sizeof(cmp)) != 0);

	pSlot->body.ceiling = s;
	snap_MoverCommit(pSlot, SNAPMV_CEILING, index, flags, bGone, bChanged);
}

// DDoor excludes the plane, its wait countdown, and botDist and oldFloorDist,
// which Tick recomputes on both machines every tic a floor moves under the door.
// topWait is compared: it is what the countdown reloads from.
void zx::netsnapshot::MoverCaptureDoor(const void * pOwner, WORD index,
	const SnapMoverDoor & s, bool bBlocked, bool bGone)
{
	BYTE flags = 0;
	SnapMoverSlot * pSlot = snap_MoverBegin(pOwner, SNAPMV_DOOR, index,
		bBlocked, bGone, flags);
	if (pSlot == NULL)
		return;

	SnapMoverDoor cmp = pSlot->body.door;
	cmp.planeD = s.planeD;
	cmp.topCountdown = s.topCountdown;
	cmp.botDist = s.botDist;
	cmp.oldFloorDist = s.oldFloorDist;
	const bool bChanged = (pSlot->cls != SNAPMV_DOOR)
		|| (pSlot->index != index)
		|| (pSlot->flags != flags)
		|| (memcmp(&cmp, &s, sizeof(cmp)) != 0);

	pSlot->body.door = s;
	snap_MoverCommit(pSlot, SNAPMV_DOOR, index, flags, bGone, bChanged);
}

// DElevator excludes both planes; everything else is constant over its life
// except the flags, so it records at creation, a block or unblock, and GONE.
void zx::netsnapshot::MoverCaptureElevator(const void * pOwner, WORD index,
	const SnapMoverElevator & s, bool bBlocked, bool bGone)
{
	BYTE flags = 0;
	SnapMoverSlot * pSlot = snap_MoverBegin(pOwner, SNAPMV_ELEVATOR, index,
		bBlocked, bGone, flags);
	if (pSlot == NULL)
		return;

	SnapMoverElevator cmp = pSlot->body.elevator;
	cmp.floorPlaneD = s.floorPlaneD;
	cmp.ceilingPlaneD = s.ceilingPlaneD;
	const bool bChanged = (pSlot->cls != SNAPMV_ELEVATOR)
		|| (pSlot->index != index)
		|| (pSlot->flags != flags)
		|| (memcmp(&cmp, &s, sizeof(cmp)) != 0);

	pSlot->body.elevator = s;
	snap_MoverCommit(pSlot, SNAPMV_ELEVATOR, index, flags, bGone, bChanged);
}

// As the elevator, `blocked` compared so a change of stalled plane records.
void zx::netsnapshot::MoverCapturePillar(const void * pOwner, WORD index,
	const SnapMoverPillar & s, bool bGone)
{
	BYTE flags = 0;
	SnapMoverSlot * pSlot = snap_MoverBegin(pOwner, SNAPMV_PILLAR, index,
		s.blocked != 0, bGone, flags);
	if (pSlot == NULL)
		return;

	SnapMoverPillar cmp = pSlot->body.pillar;
	cmp.floorPlaneD = s.floorPlaneD;
	cmp.ceilingPlaneD = s.ceilingPlaneD;
	const bool bChanged = (pSlot->cls != SNAPMV_PILLAR)
		|| (pSlot->index != index)
		|| (pSlot->flags != flags)
		|| (memcmp(&cmp, &s, sizeof(cmp)) != 0);

	pSlot->body.pillar = s;
	snap_MoverCommit(pSlot, SNAPMV_PILLAR, index, flags, bGone, bChanged);
}

// A polyobject is blocked while any entry is and gone with none; `bAllocate`
// false, for a destroy's capture, never takes a slot or revives a gone one.
void zx::netsnapshot::MoverCapturePoly(const void * pOwner, WORD index,
	const SnapMoverPoly & s, unsigned int overCap, bool bAllocate)
{
	bool bBlocked = false;
	for (unsigned int i = 0; i < s.count; ++i)
	{
		if (s.e[i].kind & SNAPPO_BLOCKED)
			bBlocked = true;
	}
	const bool bGone = (s.count == 0);

	BYTE flags = 0;
	SnapMoverSlot * pSlot = snap_MoverBegin(pOwner, SNAPMV_POLY, index,
		bBlocked, bGone, flags, bAllocate);
	if ((pSlot == NULL) || ((bAllocate == false) && (bGone == false)
		&& (pSlot->goneTic != 0)))
		return;

	if (index >= g_moverPolyBody.Size())
	{
		const unsigned int old = g_moverPolyBody.Size();
		g_moverPolyBody.Resize(index + 1);
		memset(&g_moverPolyBody[old], 0,
			(index + 1 - old) * sizeof(SnapMoverPoly));
		g_moverPolyOverLast.Resize(index + 1);
		memset(&g_moverPolyOverLast[old], 0, index + 1 - old);
		g_moverPolyResyncTic.Resize(index + 1);
		for (unsigned int i = old; i <= index; ++i)
			g_moverPolyResyncTic[i] = -1;
	}
	SnapMoverPoly & body = g_moverPolyBody[index];

	// New drops only: every action captures twice a tic.
	BYTE & overLast = g_moverPolyOverLast[index];
	if (overCap > overLast)
		g_moverPolyOverCap += overCap - overLast;
	overLast = (BYTE)overCap;

	// memcpy, so the padding the compare reads is the capture's zeroes.
	SnapMoverPoly was, now;
	memcpy(&was, &body, sizeof(was));
	memcpy(&now, &s, sizeof(now));
	ComputePolyCompareKey(was);
	ComputePolyCompareKey(now);
	bool bChanged = (pSlot->cls != SNAPMV_POLY)
		|| (pSlot->index != index)
		|| (pSlot->flags != flags)
		|| (memcmp(&was, &now, sizeof(was)) != 0);

	// The resync: a live slot with a perpetual rotation entry, once a period.
	if ((bGone == false) && (bChanged == false)
		&& (((gametic + index * 37) % SNAPMV_POLY_RESYNC_TICS) == 0)
		&& (g_moverPolyResyncTic[index] != gametic))
	{
		for (unsigned int i = 0; i < s.count; ++i)
		{
			if (((s.e[i].kind & SNAPPO_TYPEMASK) == SNAPPO_ROTATE)
				&& (s.e[i].dist == -1))
			{
				bChanged = true;
				g_moverPolyResyncTic[index] = gametic;
				++g_moverPolyResyncs;
				break;
			}
		}
	}

	memcpy(&body, &s, sizeof(body));
	snap_MoverCommit(pSlot, SNAPMV_POLY, index, flags, bGone, bChanged);
}

void zx::netsnapshot::MoverPolyStats(unsigned int & reclaimed, unsigned int & overCap,
	unsigned int & resyncs, bool bReset)
{
	reclaimed = g_moverPolyReclaimed;
	overCap = g_moverPolyOverCap;
	resyncs = g_moverPolyResyncs;
	if (bReset)
		g_moverPolyReclaimed = g_moverPolyOverCap = g_moverPolyResyncs = 0;
}

// Dropped at a map change: slot identity is a pointer, and a surviving slot
// would be matched by whatever allocation lands at that address.
void zx::netsnapshot::MoverReset()
{
	memset(g_moverSlots, 0, sizeof(g_moverSlots));
	g_moverSlotsUsed = 0;
	g_moverPolyBody.Clear();
	g_moverPolyOverLast.Clear();
	g_moverPolyResyncTic.Clear();
}

unsigned int zx::netsnapshot::MoverSlotCount()
{
	return g_moverSlotsUsed;
}

bool zx::netsnapshot::MoverSlotRead(unsigned int slot, BYTE & cls, BYTE & flags,
	WORD & index, BYTE & gen, WORD & seq, BYTE * pBody, unsigned int & bodyBytes)
{
	if (slot >= g_moverSlotsUsed)
		return false;
	const SnapMoverSlot & sl = g_moverSlots[slot];
	if (sl.owner == NULL)
		return false;
	cls = sl.cls;
	flags = sl.flags;
	if (sl.cut & SNAPMV_PLANE_FLOOR)
		flags |= SNAPMVF_NOFLOOR;
	if (sl.cut & SNAPMV_PLANE_CEILING)
		flags |= SNAPMVF_NOCEILING;
	index = sl.index;
	gen = sl.gen;
	seq = sl.seq;
	if (sl.cls == SNAPMV_POLY)
	{
		if (sl.index >= g_moverPolyBody.Size())
			return false;
		bodyBytes = WriteMoverPoly(pBody, ServerMapFrame(), g_moverPolyBody[sl.index]);
		return true;
	}
	bodyBytes = MoverBodyBytes(sl.cls, sl.flags);
	if (bodyBytes == 0)
		return false;			// a class this build cannot serialise
	if (sl.cls == SNAPMV_PLAT)
		WriteMoverPlat(pBody, sl.body.plat);
	else if (sl.cls == SNAPMV_CEILING)
		WriteMoverCeiling(pBody, sl.body.ceiling);
	else if (sl.cls == SNAPMV_FLOOR)
		WriteMoverFloor(pBody, sl.body.floor,
			(sl.flags & SNAPMVF_STAIR) != 0);
	else if (sl.cls == SNAPMV_DOOR)
		WriteMoverDoor(pBody, sl.body.door);
	else if (sl.cls == SNAPMV_ELEVATOR)
		WriteMoverElevator(pBody, sl.body.elevator);
	else if (sl.cls == SNAPMV_PILLAR)
		WriteMoverPillar(pBody, sl.body.pillar);
	return true;
}

// `bumps` close to `captures` would mean the plane is compared (DPlat captures
// twice a tic, halving its baseline).
void zx::netsnapshot::MoverCaptureStats(unsigned int & live, unsigned int & captures,
	unsigned int & bumps, unsigned int & full, unsigned int & blocked,
	unsigned int & recycled, unsigned int & recycledPlane,
	unsigned int & disowned, unsigned int & retired, unsigned int & narrowed,
	unsigned int & sweptUnstarted, unsigned int & sweptPressure,
	unsigned int & heldPast, unsigned int & heldLongest, unsigned int & heldNow,
	bool bReset)
{
	live = 0;
	heldNow = 0;
	for (unsigned int i = 0; i < g_moverSlotsUsed; ++i)
	{
		const SnapMoverSlot & sl = g_moverSlots[i];
		if (sl.owner == NULL)
			continue;
		if (sl.goneTic == 0)
			++live;
		else if ((g_moverFrame - sl.goneFrame) >= SNAPMV_GONE_HOLD_FRAMES)
			++heldNow;
	}
	captures = g_moverCaptures;
	bumps = g_moverBumps;
	full = g_moverSlotsFull;
	blocked = g_moverBlocked;
	recycled = g_moverRecycled;
	recycledPlane = g_moverRecycledPlane;
	disowned = g_moverDisowned;
	retired = g_moverRetired;
	narrowed = g_moverNarrowed;
	sweptUnstarted = g_moverSweptUnstarted;
	sweptPressure = g_moverSweptPressure;
	heldPast = g_moverHeldPast;
	heldLongest = g_moverHeldLongest;
	if (bReset)
	{
		g_moverCaptures = g_moverBumps = g_moverSlotsFull = g_moverBlocked = 0;
		g_moverRecycled = g_moverRecycledPlane = g_moverDisowned = 0;
		g_moverRetired = g_moverNarrowed = 0;
		g_moverSweptUnstarted = g_moverSweptPressure = 0;
		g_moverHeldPast = g_moverHeldLongest = 0;
	}
}

//*****************************************************************************

