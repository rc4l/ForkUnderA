// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: capture.cpp
//
// Description: The server's snapshot tick: the world capture and its
// censuses, transmission to each client, legacy suppression, cosmetic classes
// and events, snapshot sounds and mover capture. The counters it keeps are
// printed by report.cpp. See capture.h.
//
//-----------------------------------------------------------------------------

#include <string.h>
#include <math.h>		// atan2, for the straightness census
#include <stdlib.h>
#include <stdio.h>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <xmmintrin.h>	// _mm_prefetch, for the capture walk
#endif

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
#include "c_console.h"	// C_GetMessageLevel, for the report's notice
#include "network.h"
#include "i_system.h"
#include "s_sound.h"
// [Kaori-Chani] The time base: declarations here, bodies in posix/sdl/ and win32/, since the
// counter is platform-specific and these declarations are not.
#include "i_tictime.h"
#include "tarray.h"
#include "p_effect.h"
#include "features/net-snapshot/format.h"
#include "features/net-snapshot/computation/law_compute.h"
#include "features/net-snapshot/computation/join_compute.h"
#include "features/net-snapshot/computation/mover_compute.h"
#include "features/net-snapshot/computation/budget_compute.h"
#include "features/net-snapshot/computation/catchup_compute.h"
#include "features/net-snapshot/computation/propcensus_compute.h"
#include "features/net-snapshot/computation/proptell_compute.h"
#include "features/net-snapshot/computation/sound_compute.h"
#include "features/actormaterials/actormaterials.h"	// MaterialName, for a props slot
#include "features/sound-amplify/computation/sound_amplify_compute.h"
#include "features/net-snapshot/clienttrack.h"
#include "sv_main.h"
#include "sv_commands.h"
#include "s_sndseq.h"	// [Kaori-Chani] SN_CheckSequence, for the probe below
#include "network/netcommand.h"
#include "features/net-snapshot/capture.h"
#include "features/net-snapshot/report.h"	// the report this file keeps the counters for
#include "m_misc.h"	// M_GetFuaUserPath, the folder the module writes in
#include "cmdlib.h"	// CreatePath, which makes it
#include "features/net-snapshot/selftest.h"
#include "r_data/r_translate.h"	// TRANSLATION_SHIFT, NUM_TRANSLATION_TABLES
#include "p_spec.h"				// DPlat, DDoor, DScroller, DPusher, ...
#include "p_local.h"			// DPolyAction and its subclasses
#include "po_man.h"				// FPolyObj, for the polyobject dump
#include "dsectoreffect.h"	// DSectorEffect, the base the census walks
#include "p_tags.h"			// tagManager: a sector carries a list of tags here, not one short
#include "features/net-snapshot/entities.h"	// NewestGameTic, to align a client reading
#include "features/net-snapshot/ghostactor.h"	// cosmetic events: the client side of these hooks
#include "thingdef/thingdef.h"		// cosmetic classes: action functions and their parameters
#include "thingdef/thingdef_exp.h"
#include "p_acs.h"					// cosmetic classes: a script's CLIENTSIDE flag

using namespace zx::netsnapshot;

//*****************************************************************************
//	CONSOLE VARIABLES

CVAR(Bool, fua_snap, false, CVAR_NOSETBYACS)
CVAR(Int, fua_snap_stats, 0, CVAR_NOSETBYACS | CVAR_FUA_DEV_MODE)

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

// [Kaori-Chani] Tics one pump may run before the rest are dropped and game time dilates (5.4);
// 0 uncapped, and read only while the gate is on, so a gate-off server catches up as stock does.
CVAR(Int, fua_snap_maxcatchuptics, 8, CVAR_ARCHIVE | CVAR_NOSETBYACS)

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

// An actor's render style as a legacy ERenderStyle index (format.md).
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
namespace zx { namespace netsnapshot {
unsigned int g_targetEnters = 0, g_targetNoneEnters = 0;
}} // namespace zx::netsnapshot

// The class last seen at each NetID, a change meaning the id was recycled.
static WORD * g_lastClassAtId = NULL;

// Captured actors with a nonzero effects byte and the union of the bits, which
// the client's report should match.
namespace zx { namespace netsnapshot {
unsigned int	g_capEffectsActors = 0;
BYTE			g_capEffectsBits = 0;

// The map's position frame, computed on first use after a map change, and its
// clamps in this report window.
SnapFrame		g_mapFrame = FrameNarrow();
static bool				g_mapFrameDue = true;
unsigned int		g_frameClamps = 0;
}} // namespace zx::netsnapshot

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
	// [Kaori-Chani] Both raws go to QuantCrouch, which takes ints because format.h includes no
	// engine header, and a crouch between half a unit and one narrows losslessly.
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
namespace zx { namespace netsnapshot {
BYTE				g_snapMapStamp = 0;
}} // namespace zx::netsnapshot
// Map resets on this server across maps, on every SVC2_SNAPSHOT and
// DestroyAllSectorMovers; a client drops records counted before its own.
static BYTE				g_snapResetCount = 0;
// What the last DestroyAllSectorMovers carried: behind the count only while
// fua_snap_debug_resetsplit defers the send.
static BYTE				g_snapResetCountSent = 0;

//*****************************************************************************
//	COSMETIC CLASSES
//
// Each class's verdict, inert or networked with the first failed test, taken
// once per map, lazily (design doc 6.3, Classification).

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
static const char* const g_cosRowNames[SNAPCOS_ROWS] =
	{ "networked", "no server copy", "server copy kept", "inert", "plain copy", "blood" };
namespace zx { namespace netsnapshot {
BYTE*			g_cosVerdict = NULL;		// by classId
}} // namespace zx::netsnapshot
static BYTE*			g_cosFlags = NULL;			// by classId: SNAPCOSF_*
static const char**		g_cosWhy = NULL;			// by classId: the failed test
static const char**		g_cosWhat = NULL;			// by classId: its subject (function, class)
static bool				g_cosReady = false;
static BYTE				g_cosStamp = 0;
static TArray<actionf_p> g_cosSafe;					// cosmetic action functions
static TMap<const PClass*, bool> g_cosSpecialOwners;	// own a parameter expression that runs a special
static bool				g_cosOwnersBuilt = false;
static bool				g_cosFuncsFound = false;
namespace zx { namespace netsnapshot {
double			g_cosClassifyMS = 0.0;		// the last full pass
}} // namespace zx::netsnapshot
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
namespace zx { namespace netsnapshot {
unsigned int g_cosAcsClasses = 0, g_cosAcsRow1 = 0;
}} // namespace zx::netsnapshot

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
void zx::netsnapshot::snap_CosEnsure()
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
BYTE zx::netsnapshot::snap_CosRow(unsigned int id)
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

bool zx::netsnapshot::snap_CosIsCosmetic(unsigned int id)
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
namespace zx { namespace netsnapshot {
unsigned int* g_cosInheritByClass = NULL;	// networked children, by class
unsigned int g_cosInheritTotal = 0;			// networked children
unsigned int g_cosInheritOverlap = 0;		// cosmetic children: already layer 1
}} // namespace zx::netsnapshot

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
namespace zx { namespace netsnapshot {
TArray<AActor*> g_ceHeld;	// for the deadline
}} // namespace zx::netsnapshot
// The row by NetID, for the snapshot's records; set at every spawn, so a
// reused id describes its newest owner.
static BYTE*		g_cosNetRow = NULL;

// The largest generic event; the command's own header is in report.h.
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
namespace zx { namespace netsnapshot {
QWORD		g_ceGeneric[SNAPCOS_ROWS];	// generic events by row
QWORD		g_ceBlood = 0, g_ceSplatter = 0;
QWORD		g_ceByCopy = 0, g_ceDiedEarly = 0, g_ceUntold = 0, g_ceFromTick = 0;
QWORD		g_ceVelBig = 0, g_ceVelSmall = 0, g_ceStateByte = 0, g_ceStateOutside = 0;
QWORD		g_ceExtras = 0, g_ceRelaxed = 0, g_ceRemoved = 0;
unsigned int* g_ceNotInertByClass = NULL;	// rows 2 and 4, by class
QWORD		g_ceClientTics = 0, g_ceKept = 0, g_ceCulled = 0, g_ceShooterSkips = 0;
QWORD		g_ceWanted = 0, g_ceSent = 0, g_ceCapDropped = 0, g_cePumpsCapped = 0;
QWORD		g_ceClientPumps = 0, g_ceAppended = 0, g_ceAlone = 0;
QWORD		g_ceAgeSaturated = 0, g_ceBloodKept = 0, g_ceBloodUnknown = 0;
QWORD		g_ceTodayRecs[SNAPCOS_ROWS], g_ceTodayBytes[SNAPCOS_ROWS];
QWORD		g_ceCarried = 0, g_ceCarryAged = 0, g_ceCarryOver = 0, g_ceBankAtCap = 0;
QWORD		g_ceHeldN = 0, g_ceRelTick = 0, g_ceRelScript = 0, g_ceRelDeadline = 0;
QWORD		g_ceRemovedHeld = 0, g_ceScriptAfter = 0;
// Layer 1, per window: the TID rule (tid, special, args), children left with no
// producer, row-1 copies of ACS-named classes, routed copies alive at a join.
QWORD		g_ceTidRule[4], g_ceOrphaned = 0, g_ceAcsKept = 0;
QWORD		g_ceAcsRouted = 0, g_ceAcsClientSide = 0;	// routed ACS spawns
QWORD		g_ceJoins = 0, g_ceJoinAlive = 0, g_ceJoinWorst = 0;
unsigned int* g_ceAgeByClass = NULL;	// scratch for the report
// Layer 1, per map: networked map things clients could make, why the rest
// could not, and what the listed ones cost the snapshot now.
unsigned int	g_cmNetworked = 0, g_cmListed = 0, g_cmLive = 0, g_cmSolidOnly = 0;
unsigned int	g_cmRefused[4];		// marker, tid, special, args
unsigned int* g_cmListedByClass = NULL;
unsigned int* g_cmSolidByClass = NULL;
static BYTE*		g_cmByNetID = NULL;		// 1: a listed map thing, 2: solid, failing nothing else
QWORD		g_cmTodayRecs = 0, g_cmTodayBytes = 0;
QWORD		g_cmSolidRecs = 0, g_cmSolidBytes = 0;
}} // namespace zx::netsnapshot

void zx::netsnapshot::snap_CosCountersReset()
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
unsigned int zx::netsnapshot::snap_CosCountAlive(unsigned int* pPast5,
	unsigned int* pPast30, unsigned int* pTidNow)
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

// The cosmetic set, or with an argument every class whose name contains it,
// networked ones with the reason.
DEV_MODE_CCMD(fua_snap_cosmetics)
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
namespace zx { namespace netsnapshot {
QWORD		g_phaseCaptureRaw = 0;	// world diff + the capture census
QWORD		g_phaseSendRaw = 0;		// per-client priority pass and emit
QWORD		g_phaseTickRaw = 0;		// both, plus the rest of ServerTick
QWORD		g_phaseWorstRaw = 0;	// worst single tick in the window
}} // namespace zx::netsnapshot

// The wall gap between pumps and the report's cost, for stalls the phases miss.
static tictime_raw_t	g_lastPumpRaw = 0;
static int				g_lastPumpGametic = 0;
namespace zx { namespace netsnapshot {
QWORD			g_pumpGapWorstRaw = 0;
int				g_pumpGapWorstTics = 0;
QWORD			g_pumpGapSumRaw = 0;
unsigned int		g_pumpGapCount = 0;
// Reset inside the report and sampled after it, so at print time this holds
// the duration of the PREVIOUS report - the current one has not finished.
QWORD			g_reportRaw = 0;
unsigned int	g_phaseFrames = 0;
}} // namespace zx::netsnapshot

// Chunks of the current client's payload, reused across clients and frames.
static TArray<SnapChunk> g_chunks;
namespace zx { namespace netsnapshot {
QWORD		g_chunkDatagrams = 0;
QWORD		g_chunkFrames = 0;
}} // namespace zx::netsnapshot

// Link impairment in the protocol, per client, for tests without an emulator.
CVAR(Int, fua_snap_debug_ackdelay, 0, CVAR_NOSETBYACS | CVAR_FUA_DEV_MODE)	// frames an ack waits
CVAR(Int, fua_snap_debug_loss, 0, CVAR_NOSETBYACS | CVAR_FUA_DEV_MODE)		// percent dropped
// Tics to delay DestroyAllSectorMovers after a map reset (0 off), a second
// reset inside the delay restarting it.
CVAR(Int, fua_snap_debug_resetsplit, 0, CVAR_NOSETBYACS | CVAR_FUA_DEV_MODE)
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
namespace zx { namespace netsnapshot {
unsigned int g_droppedPackets = 0;
}} // namespace zx::netsnapshot

// Percent of snapshot datagrams held back and sent fua_snap_debug_holdbackgap
// later, the reproducer for a datagram arriving after it was declared lost.
CVAR(Int, fua_snap_debug_holdback, 0, CVAR_NOSETBYACS | CVAR_FUA_DEV_MODE)
#define SNAP_HOLDBACK_DATAGRAMS		12
// Datagrams a held one waits, 1 to 64: 12 is past the reorder window and
// inside the ack history, past 32 beyond it.
CVAR(Int, fua_snap_debug_holdbackgap, SNAP_HOLDBACK_DATAGRAMS, CVAR_NOSETBYACS | CVAR_FUA_DEV_MODE)
// Seconds between dumps of what each client may hold to snappresence-sv.txt,
// for tools/snapshot/presence_diff.py (0 off).
CVAR(Int, fua_snap_debug_presencelog, 0, CVAR_NOSETBYACS | CVAR_FUA_DEV_MODE)
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
namespace zx { namespace netsnapshot {
unsigned int g_heldPackets = 0;
}} // namespace zx::netsnapshot

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
namespace zx { namespace netsnapshot {
unsigned int g_reentryCount = 0;
unsigned int g_reentryReuse = 0;	// slots whose occupant changed
static BYTE* g_branchedTic = NULL;		// 1 = this id branched on the current tic
unsigned int g_branchCount = 0;	// world-wide, once per branch per tic

// Per-client observations of those branches (about N x g_branchCount at N
// clients), since distance and staleness are client facts.
unsigned int g_brDist[6];	// <512, 1k, 2k, 4k, 8k, beyond
unsigned int g_brStale[5];	// <2, <5, <10, <35, 35+ tics since position
unsigned int g_brVel[3];		// still, <8 units/tic, 8+
unsigned int g_brDeaths = 0;
unsigned int g_brDeathsNear = 0;	// deaths inside 512 units
unsigned int g_brTotal = 0;
}} // namespace zx::netsnapshot

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
namespace zx { namespace netsnapshot {
unsigned int g_runMissile[5];
unsigned int g_runOther[5];

// Whether the ballistic bit predicted right (missiles only), the previous tic's
// bit scored against whether this tic held its heading.
unsigned int g_predHeld = 0;		// bit set, heading held: extrapolation was right
unsigned int g_predTurned = 0;	// bit set, heading turned: extrapolated into a turn
unsigned int g_stepHeld = 0;		// bit clear, heading held: stepped something straight
unsigned int g_stepTurned = 0;	// bit clear, heading turned: stepped correctly
unsigned int g_straightSamples = 0;
}} // namespace zx::netsnapshot

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
namespace zx { namespace netsnapshot {
QWORD		g_huntingAccum = 0;
QWORD		g_liveAccum = 0;
// Owned inventory left out of the capture, per frame and summed for the report.
static unsigned int	g_ownedSkipped = 0;
QWORD		g_ownedAccum = 0;
unsigned int	g_huntingFrames = 0;
}} // namespace zx::netsnapshot
static BYTE* g_prevPresent = NULL;
static BYTE* g_curPresent = NULL;
static WORD* g_idleTics = NULL;
// Which classes are entering, by rate, since enters re-sent over the ack round
// trip are a large share of the budget.
namespace zx { namespace netsnapshot {
unsigned int* g_enterByClass = NULL;		// indexed by classId
}} // namespace zx::netsnapshot

// Population by class, settled ones (unchanged for SNAP_SETTLE_TICS) apart,
// which says whether a slow refresh can help (9.3).
namespace zx { namespace netsnapshot {
unsigned int* g_popByClass = NULL;
unsigned int* g_popSettledByClass = NULL;
unsigned int  g_popTotal = 0;
unsigned int  g_popSettledTotal = 0;
unsigned int  g_enterClassTotal = 0;
}} // namespace zx::netsnapshot

static TArray<SnapEntityState> g_capture;
static TArray<BYTE> g_capturePriority;		// parallel to g_capture, server-only
static TArray<BYTE>		g_deltaBuf;
static TArray<BYTE>		g_zstdBuf;




namespace zx { namespace netsnapshot {
SnapStats g_stats;
}} // namespace zx::netsnapshot

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

//*****************************************************************************
//	STREAM RECORDER (design doc appendix B)
//
// Every capture to a .zsnap file, from a full baseline, so server traffic
// replays through the client's decoder; one recording, one map.

namespace zx { namespace netsnapshot {
FILE* g_recordFile = NULL;
unsigned int	g_recordRecords = 0;
QWORD		g_recordBytes = 0;
}} // namespace zx::netsnapshot

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
//	THE CATCH-UP CLAMP

// [Kaori-Chani] The run of dropping pumps, and the window's counts for the report.
static SnapCatchUpRun	g_catchRun = { 0, 0, 0, 0, -(TICRATE * SNAPCATCHUP_WARN_SECS) };
static unsigned int		g_catchDropped = 0;		// tics dropped over the window
static unsigned int		g_catchPumps = 0;		// pumps that dropped any
static unsigned int		g_catchWorst = 0;		// the most one pump dropped
static unsigned int		g_catchRuns = 0;		// runs begun
static int				g_catchWorstPercent = 100;	// the slowest the run's game time read

// The tics this pump may run. Gate off it is every tic the clock passed, as stock catches up; on,
// the rest are dropped and game time dilates rather than the world running behind its own clock.
unsigned int zx::netsnapshot::CatchUpTics(unsigned int deltaTics, unsigned int nowMS)
{
	if (snap_GateOn() == false)
	{
		g_catchRun.pumps = 0;
		return deltaTics;
	}

	const bool bWasRunning = (g_catchRun.pumps > 0);
	const SnapCatchUpOut out = ComputeCatchUp(deltaTics, fua_snap_maxcatchuptics, nowMS,
		gametic, level.maptime, TICRATE, g_catchRun);
	if (out.dropped > 0)
	{
		g_catchDropped += out.dropped;
		++g_catchPumps;
		if (out.dropped > g_catchWorst)
			g_catchWorst = out.dropped;
		if (bWasRunning == false)
			++g_catchRuns;
		if (out.percent < g_catchWorstPercent)
			g_catchWorstPercent = out.percent;
	}
	if (out.bWarn)
	{
		snap_Warn("Server overloaded: game time is running at %d%% of real time. "
			"(Simulation cannot sustain %d Hz at current load.)\n", out.percent, TICRATE);
	}
	return out.tics;
}

// What the window's clamp did, for the report; reset on read.
void zx::netsnapshot::snap_CatchUpStats(unsigned int& dropped, unsigned int& pumps,
	unsigned int& worst, unsigned int& runs, int& worstPercent)
{
	dropped = g_catchDropped;
	pumps = g_catchPumps;
	worst = g_catchWorst;
	runs = g_catchRuns;
	worstPercent = g_catchWorstPercent;
	g_catchDropped = g_catchPumps = g_catchWorst = g_catchRuns = 0;
	g_catchWorstPercent = 100;
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
//	WHERE THE MODULE WRITES

bool zx::netsnapshot::SnapFileNameBad(const char *pszName)
{
	if ((pszName == NULL) || (*pszName == '\0'))
		return true;
	for (const char *p = pszName; *p != '\0'; ++p)
		if ((*p == '/') || (*p == '\\') || (*p == ':'))
			return true;
	return false;
}

FString zx::netsnapshot::SnapFileFind(const char *pszName)
{
	if (SnapFileNameBad(pszName))
		return FString(pszName);		// a path of its own: taken as it is
	const FString path = SnapFilePath(pszName);
	FILE *f = fopen(path.GetChars(), "rb");
	if (f == NULL)
		return FString(pszName);
	fclose(f);
	return path;
}

FString zx::netsnapshot::SnapFilePath(const char *pszName)
{
	FString dir = M_GetFuaUserPath();
	dir << "/net-snapshot";
	CreatePath(dir.GetChars());
	FString path = dir;
	path << "/" << pszName;
	return path;
}

//*****************************************************************************
//	ENTITY PROPERTIES
//
// A mirror is spawned from its class, so only what a script moved away from that class
// has to travel. One slot per such entity, walked by the emitter.

namespace {
struct SnapPropSlot
{
	WORD		netID;		// 0 for a free slot
	WORD		gen;		// bumped when any carried property moves
	WORD		seen;		// the capture pass that last visited it
	SnapProps	props;
};
}
// The wire's slot count is the materials feature's own, which is where the values come
// from: a build that raised one and not the other would send part of a set.
typedef char snap_assert_prop_uniform_slots[
	(SNAP_PROPS_UNIFORM_SLOTS == zx::actormaterials::kActorUniformVec4s) ? 1 : -1];

static SnapPropSlot	g_propSlots[SNAPPROP_MAXSLOTS];
// [Kaori-Chani] NetID to slot + 1, 0 being none, so the capture needs no scan.
static WORD			g_propByID[SNAP_MAXID];
// What was told for this id, so a set that never changes needs no slot: 0 is nothing told,
// written when an id leaves and at PropForget, and never a real mark's value.
static DWORD		g_propMark[SNAP_MAXID];
// Every live marked entity, told or not, which every client walks in full every frame, so a
// set reaches one that is sent no record of its own (capture.md).
static TArray<WORD>	g_propMarkedIds;
static unsigned int	g_propLive = 0, g_propChanges = 0;
static unsigned int	g_propOverflow = 0, g_propTruncated = 0;
static WORD			g_propPass = 0;
namespace zx { namespace netsnapshot {
unsigned int	g_propMarked = 0;	// sets marked to be told, needing no slot
}} // namespace zx::netsnapshot
// [Kaori-Chani] The walk's size, counted rather than timed: a counter read an actor costs the
// order of the walk itself, and the sweep below is the only part a bracket can hold.
static QWORD		g_propWalks = 0, g_propSteps = 0;
static QWORD		g_propSweepRaw = 0;
// Generations come from here, never from the slot: a slot freed and taken by another
// entity must not reuse a number a client has already been sent for that slot.
static WORD			g_propGenNext = 0;

static WORD snap_PropGen()
{
	// 0 is what every client's array starts at, meaning nothing sent.
	++g_propGenNext;
	if (g_propGenNext == 0)
		++g_propGenNext;
	return g_propGenNext;
}

// [Kaori-Chani] Whether anything is off the class defaults at all, answered from the fields
// rather than by building the record, which is what the common entity costs.
static bool snap_PropsOffDefaults(const AActor * pActor)
{
	const AActor * pDef = pActor->GetDefault();
	return (pActor->scaleX != pDef->scaleX) || (pActor->scaleY != pDef->scaleY)
		|| (pActor->roll != pDef->roll) || (pActor->fua_material != pDef->fua_material)
		|| (pActor->fua_materialSlot != 0);
}

// What this actor carries away from its class: scale, roll and a material a script set.
static SnapProps snap_PropsOf(const AActor * pActor)
{
	SnapProps p;
	memset(&p, 0, sizeof(p));
	const AActor * pDef = pActor->GetDefault();
	if ((pActor->scaleX != pDef->scaleX) || (pActor->scaleY != pDef->scaleY))
	{
		p.flags |= SNAP_PROPS_SCALE;
		p.scaleX = QuantScale(pActor->scaleX);
		p.scaleY = QuantScale(pActor->scaleY);
	}
	if (pActor->roll != pDef->roll)
	{
		p.flags |= SNAP_PROPS_ROLL;
		p.roll = (WORD)(pActor->roll >> 16);
	}
	if (pActor->fua_material != pDef->fua_material)
	{
		// By name, not by index: a name needs no agreement about the order the
		// two ends read their GLDEFS in, and it travels once per change.
		p.flags |= SNAP_PROPS_MATERIAL;
		const char * pszName = zx::actormaterials::MaterialName(pActor->fua_material - 1);
		if (pszName != NULL)
		{
			unsigned int n = 0;
			while ((pszName[n] != '\0') && (n < SNAP_PROPS_MATNAME_MAX))
			{
				p.material[n] = pszName[n];
				++n;
			}
			if (pszName[n] != '\0')
				++g_propTruncated;
			p.matLen = (BYTE)n;
		}
	}
	// Its own uniform values, which only a script gives it: the set mask and the
	// rows, so an actor with none costs nothing.
	{
		unsigned set = 0;
		float values[SNAP_PROPS_UNIFORM_SLOTS][4];
		if (zx::actormaterials::ActorUniforms(pActor, set, values))
		{
			p.flags |= SNAP_PROPS_UNIFORMS;
			p.uniSet = (BYTE)(set & 0xFF);
			memcpy(p.uni, values, sizeof(p.uni));
		}
	}
	return p;
}

// Whether two sets differ, which is what bumps a generation.
static bool snap_PropsDiffer(const SnapProps & a, const SnapProps & b)
{
	if ((a.flags != b.flags) || (a.scaleX != b.scaleX) || (a.scaleY != b.scaleY)
		|| (a.roll != b.roll) || (a.matLen != b.matLen) || (a.uniSet != b.uniSet))
		return true;
	if (memcmp(a.material, b.material, a.matLen) != 0)
		return true;
	// Only the rows the mask covers: an unset row is never written or read.
	for (unsigned int u = 0; u < SNAP_PROPS_UNIFORM_SLOTS; ++u)
		if (((a.uniSet & (1u << u)) != 0)
			&& (memcmp(a.uni[u], b.uni[u], sizeof(a.uni[u])) != 0))
			return true;
	return false;
}

// The mark of the record a set would send, over the bytes that travel.
static DWORD snap_PropMarkOf(const SnapProps & props)
{
	BYTE body[SNAP_MAX_EVENT_BYTES];
	const unsigned int n = WritePropsBody(body, props);
	return (DWORD)ComputePropMark(body, n);
}

// The one path that needs a free slot, which runs for an entity entering the table rather
// than for every entity every tic.
static bool snap_PropTakeSlot(WORD id, const SnapProps & live)
{
	unsigned int free = SNAPPROP_MAXSLOTS;
	for (unsigned int i = 0; i < SNAPPROP_MAXSLOTS; ++i)
	{
		if (g_propSlots[i].netID == 0)
		{
			free = i;
			break;
		}
	}
	g_propSteps += (QWORD)((free == SNAPPROP_MAXSLOTS) ? SNAPPROP_MAXSLOTS : free + 1);
	if (free == SNAPPROP_MAXSLOTS)
	{
		++g_propOverflow;
		return false;
	}
	g_propSlots[free].netID = id;
	g_propSlots[free].gen = snap_PropGen();
	g_propSlots[free].seen = g_propPass;
	g_propSlots[free].props = live;
	g_propByID[id] = (WORD)(free + 1);
	++g_propChanges;
	return true;
}

// One captured entity, from the sweep. A slot is kept while the entity lives, since a set
// going back to the class defaults has to travel as well.
static void snap_PropCapture(const AActor * pActor)
{
	const WORD id = (WORD)pActor->NetID;
	++g_propWalks;
	// [Kaori-Chani] The index answers in one step, so an entity already in the table costs the
	// same whatever the table holds.
	const WORD at = (id != 0) ? g_propByID[id] : (WORD)0;
	if (at != 0)
	{
		SnapPropSlot & slot = g_propSlots[at - 1];
		++g_propSteps;
		slot.seen = g_propPass;
		const SnapProps live = snap_PropsOf(pActor);
		if (snap_PropsDiffer(slot.props, live))
		{
			slot.props = live;
			slot.gen = snap_PropGen();
			++g_propChanges;
		}
		return;
	}
	// An entity with no id of its own can be told to nobody, so there is nothing to keep.
	if (id == 0)
		return;
	// The class is all a mirror needs -- unless a set was told for this id, when a return
	// to the class defaults has to travel as well, so the mark is read first.
	const DWORD mark = g_propMark[id];
	const bool off = snap_PropsOffDefaults(pActor);
	if ((mark == SNAPPROP_MARK_NONE) && (off == false))
		return;
	const SnapProps live = snap_PropsOf(pActor);
	const DWORD body = snap_PropMarkOf(live);
	const SnapPropAction act = ComputePropTellOrSlot(mark, off, live.flags != 0, body);
	if (act == SNAPPROPACT_NOTHING)
		return;
	if (act == SNAPPROPACT_TELL)
	{
		// Told once to each client that holds it, by the per-client walk, which waits on no
		// record of its own.
		g_propMark[id] = body;
		g_propMarkedIds.Push(id);
		++g_propMarked;
		return;
	}
	snap_PropTakeSlot(id, live);
}

// Slots whose entity the sweep did not visit: it has left, and a re-enter builds its
// mirror from the class again.
static void snap_PropSweepEnd()
{
	g_propLive = 0;
	for (unsigned int i = 0; i < SNAPPROP_MAXSLOTS; ++i)
	{
		if (g_propSlots[i].netID == 0)
			continue;
		if (g_propSlots[i].seen != g_propPass)
		{
			g_propByID[g_propSlots[i].netID] = 0;
			memset(&g_propSlots[i], 0, sizeof(g_propSlots[i]));
			continue;
		}
		++g_propLive;
	}

	// The marked list, pruned where the mark has gone or the table has taken the id over,
	// so the per-client walk covers the live marked entities and no more.
	for (unsigned int i = 0; i < g_propMarkedIds.Size(); )
	{
		const WORD id = g_propMarkedIds[i];
		if ((g_propMark[id] == SNAPPROP_MARK_NONE) || (g_propByID[id] != 0))
		{
			g_propMarkedIds[i] = g_propMarkedIds[g_propMarkedIds.Size() - 1];
			g_propMarkedIds.Pop();
			continue;
		}
		++i;
	}
}

bool zx::netsnapshot::PropSlotId(unsigned int slot, WORD & netID, WORD & gen)
{
	if ((slot >= SNAPPROP_MAXSLOTS) || (g_propSlots[slot].netID == 0))
		return false;
	netID = g_propSlots[slot].netID;
	gen = g_propSlots[slot].gen;
	return true;
}

bool zx::netsnapshot::PropSlotRead(unsigned int slot, WORD & netID, SnapProps & props,
	WORD & gen)
{
	// The same guards, so a walk that passed PropSlotId reads the set it has just tested.
	if (PropSlotId(slot, netID, gen) == false)
		return false;
	props = g_propSlots[slot].props;
	return true;
}

unsigned int zx::netsnapshot::PropSlots()
{
	return SNAPPROP_MAXSLOTS;
}

void zx::netsnapshot::PropForget()
{
	memset(g_propSlots, 0, sizeof(g_propSlots));
	memset(g_propByID, 0, sizeof(g_propByID));
	memset(g_propMark, 0, sizeof(g_propMark));
	g_propMarkedIds.Clear();
	g_propLive = 0;
}

bool zx::netsnapshot::PropsToTell(WORD netID, SnapProps & out)
{
	// A slot carries its own set, so telling would send it twice; and a mark is what says
	// the capture has seen this one at all.
	if ((netID == 0) || (netID >= SNAP_MAXID) || (g_propByID[netID] != 0)
		|| (g_propMark[netID] == SNAPPROP_MARK_NONE))
		return false;
	const AActor * pActor = g_ActorNetIDList.findPointerByID(netID);
	if (pActor == NULL)
		return false;
	// Read live rather than from the mark: a re-entry and a joiner both need what the
	// entity carries now, and a difference the capture has not seen yet takes a slot next pump.
	out = snap_PropsOf(pActor);
	return (out.flags != 0);
}

const WORD * zx::netsnapshot::PropsMarkedIds(unsigned int & count)
{
	count = g_propMarkedIds.Size();
	return (count > 0) ? &g_propMarkedIds[0] : NULL;
}

void zx::netsnapshot::PropStats(unsigned int & live, unsigned int & changes,
	unsigned int & overflow, unsigned int & truncated, QWORD & walks, QWORD & steps,
	double & sweepMS, bool reset)
{
	live = g_propLive;
	changes = g_propChanges;
	overflow = g_propOverflow;
	truncated = g_propTruncated;
	walks = g_propWalks;
	steps = g_propSteps;
	sweepMS = snap_RawToMS(g_propSweepRaw);
	if (reset)
	{
		g_propChanges = g_propOverflow = g_propTruncated = 0;
		g_propMarked = 0;
		g_propWalks = g_propSteps = g_propSweepRaw = 0;
	}
}

// The classes holding the table's slots, most first, with what each slot carries.
// On demand rather than kept per window: a pump pays nothing for a command nobody ran.
DEV_MODE_CCMD(fua_snap_propclasses)
{
	if (NETWORK_GetState() != NETSTATE_SERVER)
	{
		Printf("fua_snap_propclasses is only available on a server.\n");
		return;
	}

	SnapPropSlotSeen seen[SNAPPROP_MAXSLOTS];
	unsigned int nSeen = 0, nGone = 0;
	for (unsigned int i = 0; i < SNAPPROP_MAXSLOTS; ++i)
	{
		if (g_propSlots[i].netID == 0)
			continue;
		// An entity gone since the capture leaves its slot until the sweep, so it is
		// counted without a name rather than skipped.
		const AActor * pActor = g_ActorNetIDList.findPointerByID(g_propSlots[i].netID);
		seen[nSeen].classId = (pActor != NULL)
			? (unsigned short)pActor->GetClass()->getActorNetworkIndex() : 0;
		seen[nSeen].flags = g_propSlots[i].props.flags;
		if (pActor == NULL)
			++nGone;
		++nSeen;
	}

	unsigned int live = 0, changes = 0, overflow = 0, truncated = 0;
	QWORD walks = 0, steps = 0;
	double sweepMS = 0.0;
	// Read without the reset, so the report's own window is left alone.
	PropStats(live, changes, overflow, truncated, walks, steps, sweepMS, false);
	Printf("[snap] property slots: %u of %u live | %u refused since the last report"
		" | %u names cut\n", nSeen, (unsigned int)SNAPPROP_MAXSLOTS, overflow, truncated);
	if (nSeen == 0)
		return;

	SnapPropClassCount byClass[SNAPPROP_MAXSLOTS];
	unsigned int nClasses = 0;
	ComputePropClassCensus(seen, nSeen, byClass, SNAPPROP_MAXSLOTS, nClasses);
	unsigned int total[SNAPPROPCENSUS_BITS] = { 0 };
	for (unsigned int c = 0; c < nClasses; ++c)
	{
		const PClass * pType = (byClass[c].classId != 0)
			? NETWORK_GetClassFromIdentification(byClass[c].classId) : NULL;
		Printf("  %-26s %4u  scale %u, roll %u, material %u, uniforms %u\n",
			(pType != NULL) ? pType->TypeName.GetChars() : "(gone)", byClass[c].slots,
			byClass[c].byBit[0], byClass[c].byBit[1], byClass[c].byBit[2], byClass[c].byBit[3]);
		for (unsigned int b = 0; b < SNAPPROPCENSUS_BITS; ++b)
			total[b] += byClass[c].byBit[b];
	}
	Printf("[snap] property slots by property: scale %u, roll %u, material %u,"
		" uniforms %u | %u classes, %u entities gone\n", total[0], total[1], total[2],
		total[3], nClasses, nGone);
}

//*****************************************************************************
//	THE CAPTURE PASS

// [Kaori-Chani] Starts loading the cache line at p, a hint that never faults, as in gamecopy.
static inline void snap_Prefetch(const void * p)
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	_mm_prefetch((const char *)p, _MM_HINT_T0);
#elif defined(__GNUC__) || defined(__clang__)
	__builtin_prefetch(p);
#else
	(void)p;
#endif
}

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
		// [Kaori-Chani] The step moves into the header because the body's continues would
		// otherwise never reach it.
		AActor* pAhead = iterator.Next();
		for (AActor* pActor = pAhead; pActor != NULL; pActor = pAhead)
		{
			// [Kaori-Chani] The next actor fetched while this one is captured: the walk's cost is
			// the width of an actor rather than the work done on it.
			pAhead = iterator.Next();
			for (unsigned int line = 0; (pAhead != NULL) && (line < 256); line += 64)
				snap_Prefetch((const char *)pAhead + line);
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
			// Its properties, where a script moved any off the class defaults.
			snap_PropCapture(pActor);
		}
	}
	// [Kaori-Chani] One pass a tic, so the bracket costs nothing beside it.
	const tictime_raw_t sweepStart = TIC_GetRawCounter();
	snap_PropSweepEnd();
	g_propSweepRaw += (QWORD)(TIC_GetRawCounter() - sweepStart);
	++g_propPass;

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
			// The next entity on this id is told its own set, not read as a change.
			g_propMark[id] = SNAPPROP_MARK_NONE;
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
namespace zx { namespace netsnapshot {
QWORD		g_sentBytes = 0;

// Priority accounting over the report window.
QWORD		g_tierAccum[SNAPCL_TIERS_REPORT];
QWORD		g_forcedAccum = 0;
QWORD		g_cosmeticAccum = 0;
QWORD		g_deferredAccum = 0;
unsigned int	g_priorityFrames = 0;
unsigned int	g_sentPackets = 0;

// Wire accounting: the payload against what was sent, whose peak ratio sizes
// the budget's headroom.
QWORD		g_wirePayloadBytes = 0;		// uncompressed, as handed to the transport
QWORD		g_wireSentBytes = 0;		// after Huffman/Zstandard
unsigned int	g_wirePackets = 0;
unsigned int	g_wireZstdPackets = 0;
unsigned int	g_wirePeak = 0;
QWORD		g_freshAccum = 0;
QWORD		g_resendAccum = 0;
QWORD		g_resendByteAccum = 0;
QWORD		g_tierSizeAccum[SNAPCL_TIERS_REPORT];
QWORD		g_resAllowAccum = 0, g_resSpentAccum = 0;
QWORD		g_resCappedAccum = 0, g_resDrainedAccum = 0;
}} // namespace zx::netsnapshot

// Clients with a snapshot queued in their unreliable buffer this tic, so the
// per-client send rate can be checked against the tic rate.
static bool			g_snapshotQueued[MAXPLAYERS] = { false };


// The packet room at the configured packet size (ComputePacketRoom).
unsigned int zx::netsnapshot::snap_PacketRoom()
{
	return ComputePacketRoom((unsigned int)SERVER_GetMaxPacketSize());
}

// The frame's budget at the set fua_snap_maxbytes (ComputeEffectiveBudget).
unsigned int zx::netsnapshot::snap_EffectiveBudget()
{
	return ComputeEffectiveBudget(fua_snap_maxbytes);
}

// One datagram's share of the budget at the configured packet size
// (ComputeChunkLimit).
unsigned int zx::netsnapshot::snap_ChunkLimit()
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
		// each, and a dropped one counted as if it left (capture.md).
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

bool zx::netsnapshot::TeleportFogs(AActor *pThing, AActor **pFogs)
{
	// Asked of the thing, not of a fog: whether the client can still place its own.
	const bool bServerFogs = SuppressesLegacy(pThing, SNAPLEGACY_MOTION);
	for (unsigned int i = 0; (pFogs != NULL) && (i < 2); ++i)
		ClientEffect(pFogs[i], bServerFogs, SNAPLEGACY_MOTION);
	return bServerFogs;
}

bool zx::netsnapshot::ClientEffect(AActor *pEffect, bool bSend, int level)
{
	if ((pEffect == NULL) || (NETWORK_GetState() != NETSTATE_SERVER))
		return false;

	// Asked before the id goes, so the answer is exactly where stock's own spawn
	// would have refused this effect.
	if (SuppressesLegacy(pEffect, level) == false)
		return false;

	// Out of the capture sweep, which takes every actor holding an id, and out of
	// the full update, which NETFL_ALLOWCLIENTSPAWN answers for.
	pEffect->NetworkFlags |= NETFL_ALLOWCLIENTSPAWN;
	if (pEffect->NetID != 0)
		pEffect->FreeNetID();
	// Through the stock sender, so a position whole units would move still spawns
	// exactly (ComputeSpawnRoundsPosition) and the id-less form is its choice.
	if (bSend)
		SERVERCOMMANDS_SpawnThing(pEffect);
	return true;
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
namespace zx { namespace netsnapshot {
BYTE g_soundIdsAnswer[MAXPLAYERS];
}} // namespace zx::netsnapshot

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

namespace zx { namespace netsnapshot {
unsigned int g_soundSent = 0, g_soundCulled = 0, g_soundPositional = 0;
unsigned int g_soundRoutedPoint = 0;	// NetID-less routed cosmetics, as points
}} // namespace zx::netsnapshot

// Sounds by table index rather than name, to a client whose first N names match
// the server's (a pack loaded before the server's files shifts them).
CVAR(Bool, fua_snap_soundids, true, CVAR_NOSETBYACS)

namespace zx { namespace netsnapshot {
unsigned int	g_soundById = 0, g_soundByName = 0;
// Why a snapshot sound went by name, and sounds never sent at all.
unsigned int	g_soundNameRange = 0, g_soundNameChannel = 0;
unsigned int	g_soundNameOff = 0, g_soundNameUnmatched = 0;
unsigned int	g_soundUnplayable = 0;
}} // namespace zx::netsnapshot

void zx::netsnapshot::SendPlayerStopSound(AActor *pActor, int channel)
{
	// No state record can carry a stop and a mirror runs no states, so the gate owes a player
	// one; with the gate off stock's own sends stand and this adds nothing.
	if ((NETWORK_GetState() != NETSTATE_SERVER) || (pActor == NULL)
		|| (pActor->player == NULL) || (snap_GateOn() == false))
		return;
	SERVERCOMMANDS_StopSound(pActor, channel);
}

bool zx::netsnapshot::SoundUnplayable(int soundId, float volume)
{
	// Gate off, stock's own sends, so a silent one still goes out as it always did.
	if (snap_GateOn() == false)
		return false;
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
	SnapSoundWire snd;
	snd.stamp = g_snapMapStamp;
	snd.bPoint = bPoint;
	// A point drops the loop, as SoundPoint's Byte channel does, which the codec
	// settles for both ends.
	snd.bLoop = ((channel & CHAN_LOOP) != 0);
	snd.x = pActor->x;
	snd.y = pActor->y;
	snd.z = pActor->z;
	// A point carries the NetID too: the client moves the sound onto the mirror
	// once one exists, so it travels with the entity as stock's does.
	snd.netID = (unsigned short)pActor->NetID;
	snd.channel = channel;
	snd.soundId = soundId;
	snd.volume = ComputeSoundVolumeToWire(volume);
	snd.atten = NETWORK_AttenuationFloatToInt(attenuation);

	BYTE body[SNAP_SOUND_MAX_BYTES];
	const unsigned int bytes = WriteSnapSound(body, ServerMapFrame(), snd);
	NetCommand command(SVC2_SNAPSOUND);
	command.addBuffer(body, bytes);
	if (pActor->NetID == 0)
		command.setUnreliable(true);
	command.sendCommandToOneClient(client);
}

// The codec's attenuation default is the engine's, which a pure unit cannot
// include to check for itself.
typedef char snap_assert_sound_atten[(SNAP_SOUND_ATTEN_NORM == ATTN_INT_NORM) ? 1 : -1];

// Check if the client is in the server and send the records it needs.
bool zx::netsnapshot::snap_ClientInMap(unsigned int client)
 {
	if (SERVER_IsValidClient(client) == false)
		return false;

	const CLIENT_s * pClient = SERVER_GetClient(client);
	return (pClient != NULL) && (pClient->State == CLS_SPAWNED);
}

bool zx::netsnapshot::BroadcastActorSound(AActor *pActor, int channel, int soundId,
	float volume, float attenuation, bool bSoundOnClient, bool bNoStop, int pitch)
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
				// Stock's own call carries these two, so the fallback does as well; the
				// index route has no field for either yet.
				SERVERCOMMANDS_SoundActor(pActor, channel, pszName, volume, attenuation,
					i, SVCF_ONLYTHISCLIENT, bNoStop, pitch);
		}
		else
		{
			++g_soundPositional;
			if (bById)
				snap_SendSoundId(i, pActor, true, channel, soundId, volume, attenuation);
			else
				// [Kaori-Chani] The stock command's coordinates are LONG, so a 48.16 position
				// is cast as every other engine caller casts it: the raw, truncated to 32 bits.
				SERVERCOMMANDS_SoundPoint((LONG)pActor->x, (LONG)pActor->y, (LONG)pActor->z,
					channel, pszName, volume, attenuation, i, SVCF_ONLYTHISCLIENT, pitch);
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
// Mover sounds sent, by the command that carried them.
static unsigned int	g_ceilSoundBySector = 0;
static unsigned int	g_ceilSoundLegacy = 0;
static unsigned int	g_platSoundBySector = 0;
static unsigned int	g_platSoundLegacy = 0;
static unsigned int	g_elevSoundBySector = 0;
static unsigned int	g_elevSoundLegacy = 0;
// No legacy counterpart: stock sends no pillar sound command at all.
static unsigned int	g_pillarSoundBySector = 0;

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

void zx::netsnapshot::MoverSendElevatorSound( sector_t * sec, int elevatorID )
{
	if ( NETWORK_GetState() != NETSTATE_SERVER )
		return;
	// Gate off, stock's own command, so the bytes match. Its one site broadcasts, and
	// DElevator::UpdateToClient sends no sound at all, so a joiner gets none either way.
	if ( g_moverGate )
	{
		SERVERCOMMANDS_MoverSound( sec, SNAPSND_PACK( SNAPSND_ELEVATOR, 0 ));
		++g_elevSoundBySector;
	}
	else
	{
		SERVERCOMMANDS_StartElevatorSound( elevatorID );
		++g_elevSoundLegacy;
	}
}

void zx::netsnapshot::MoverSendPillarSound( sector_t * sec, unsigned int client,
	unsigned int flags )
{
	if (( NETWORK_GetState() != NETSTATE_SERVER ) || ( g_moverGate == false ))
		return;
	// client_DoPillar started this sound itself, so with the gate off there is nothing to
	// send and nothing to match: the recipients are DoPillar's, which this mirrors.
	SERVERCOMMANDS_MoverSound( sec, SNAPSND_PACK( SNAPSND_PILLAR, 0 ),
		client, ServerCommandFlags::FromInt( flags ));
	++g_pillarSoundBySector;
}

void zx::netsnapshot::MoverSoundStats( unsigned int & bySector, unsigned int & legacy,
	bool bReset )
{
	bySector = g_ceilSoundBySector + g_platSoundBySector + g_elevSoundBySector
		+ g_pillarSoundBySector;
	legacy = g_ceilSoundLegacy + g_platSoundLegacy + g_elevSoundLegacy;
	if ( bReset )
	{
		g_ceilSoundBySector = g_ceilSoundLegacy = 0;
		g_platSoundBySector = g_platSoundLegacy = 0;
		g_elevSoundBySector = g_elevSoundLegacy = 0;
		g_pillarSoundBySector = 0;
	}
}

void zx::netsnapshot::MoverSoundStatsByKind( unsigned int * bySector, unsigned int * legacy,
	bool bReset )
{
	bySector[SNAPSND_CEILING] = g_ceilSoundBySector;
	bySector[SNAPSND_PLAT] = g_platSoundBySector;
	bySector[SNAPSND_ELEVATOR] = g_elevSoundBySector;
	bySector[SNAPSND_PILLAR] = g_pillarSoundBySector;
	legacy[SNAPSND_CEILING] = g_ceilSoundLegacy;
	legacy[SNAPSND_PLAT] = g_platSoundLegacy;
	legacy[SNAPSND_ELEVATOR] = g_elevSoundLegacy;
	if ( bReset )
	{
		g_ceilSoundBySector = g_ceilSoundLegacy = 0;
		g_platSoundBySector = g_platSoundLegacy = 0;
		g_elevSoundBySector = g_elevSoundLegacy = 0;
		g_pillarSoundBySector = 0;
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
		PropForget();
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

	// One queued line a pump, after the timing, at the level it was queued with.
	snap_DrainOneLine();
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

	// One folder for every file the module writes, so a recording never lands in
	// whatever directory a server happened to start in.
	const char* pszName = argv[1];
	if (SnapFileNameBad(pszName))
	{
		Printf("fua_snap_record: filename must not contain path separators.\n");
		return;
	}

	FString filename = pszName;
	if (filename.IndexOf('.') == -1)
		filename += ".zsnap";
	const FString path = SnapFilePath(filename.GetChars());

	g_recordFile = fopen(path.GetChars(), "wb");
	if (g_recordFile == NULL)
	{
		Printf("fua_snap_record: couldn't open '%s' for writing.\n", path.GetChars());
		return;
	}
	Printf("fua_snap_record: writing %s\n", path.GetChars());

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
// sector index (capture.md).
CVAR(Int, fua_csnap_planetrace, 0, CVAR_NOSETBYACS | CVAR_FUA_DEV_MODE)	// -1 all, 0 off

// Report of the sector link graph, taken by the join sector pass.
CVAR(Bool, fua_snap_linkcensus, false, CVAR_NOSETBYACS | CVAR_FUA_DEV_MODE)

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
// Dumps sector planes by index to diff across machines (capture.md): every
// mover or tagged sector, `all`, or one index and its linked sectors.
DEV_MODE_CCMD(fua_snap_planedump)
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
			// [Kaori-Chani] A sector carries a list of tags here, so "untagged" asks the tag
			// manager, to which a sector tagged 0 and one with no tag at all differ.
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
DEV_MODE_CCMD(fua_snap_soundcount)
{
	unsigned int bySector[SNAPSND_COUNT] = { 0 }, legacy[SNAPSND_COUNT] = { 0 };
	zx::netsnapshot::MoverSoundStatsByKind(bySector, legacy, true);
	Printf("[snap] mover sounds since the last read: ceiling %u by sector, %u legacy"
		" | plat %u by sector, %u legacy | elevator %u by sector, %u legacy"
		" | pillar %u by sector, none legacy\n",
		bySector[SNAPSND_CEILING], legacy[SNAPSND_CEILING],
		bySector[SNAPSND_PLAT], legacy[SNAPSND_PLAT],
		bySector[SNAPSND_ELEVATOR], legacy[SNAPSND_ELEVATOR],
		bySector[SNAPSND_PILLAR]);
}

//*****************************************************************************
//
// A polyobject the sector check cannot reach: its axes, the actions on its list and whether
// its sound sequence is playing, read the same way on either end.
DEV_MODE_CCMD(fua_snap_polycheck)
{
	if (argv.argc() < 2)
	{
		Printf("usage: fua_snap_polycheck <polyobject number>\n");
		return;
	}

	const int want = atoi(argv[1]);
	FPolyObj * const poly = PO_GetPolyobj(want);
	if (poly == NULL)
	{
		Printf("no polyobject %d\n", want);
		return;
	}

	if (NETWORK_InClientMode())
		Printf("[client] gametic %d, newest server tic %d\n",
			gametic, NewestGameTic());
	else
		Printf("[server] gametic %d\n", gametic);

	Printf("poly %d (tag %d): spot %d,%d angle %u | sequence %s\n",
		want, poly->tag, (int)poly->StartSpot.x, (int)poly->StartSpot.y,
		(unsigned int)poly->angle,
		(SN_CheckSequence(poly) != NULL) ? "playing" : "none");

	unsigned int n = 0;
	for (DPolyAction * a = poly->snapActions; a != NULL; a = a->m_SnapNext)
	{
		Printf("  action %s: gen %d, seen %d, legacy %d, blocked %d\n",
			a->GetClass()->TypeName.GetChars(), (int)a->m_SnapGen,
			a->m_SnapSeen ? 1 : 0, a->m_SnapLegacy ? 1 : 0, a->m_SnapBlocked ? 1 : 0);
		++n;
	}
	if (n == 0)
		Printf("  no actions on its list\n");
}

//*****************************************************************************
//
DEV_MODE_CCMD(fua_snap_sectorcheck)
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
	// [Kaori-Chani] A sector holds a list of tags rather than one, so P_FindSectorFromTag's walk
	// is the tag manager's iterator here, as cl_main.cpp's colour and light commands walk it.
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
			if (p->IsKindOf(RUNTIME_CLASS(DDoor)))
			{
				DDoor * const dr = static_cast<DDoor *>(p);
				// Direction 0 is the wait itself, so a window polls for it before placing
				// a drop; countdown is the tics of that wait still to run.
				Printf("    direction %d countdown %d wait %d\n",
					dr->GetDirection(), dr->GetTopCountdown(), dr->GetTopWait());
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
// which Tick recomputes on both machines every tic a floor moves under the door;
// all four still ride the record. topWait is compared: the countdown reloads from it.
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

