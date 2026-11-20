//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: movers.cpp
//
// Description: The snapshot path's sector movers: the client's record
// sink, hold and reset window, the movers' capture and apply, plane
// ownership, the join's link order, and the scroller and pusher state. See
// movers.h.
//
//-----------------------------------------------------------------------------

#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_lnspec.h"
#include "p_3dmidtex.h"
#include "s_sound.h"
#include "s_sndseq.h"
#include "r_state.h"
#include "c_console.h"
#include "network.h"
#include "sv_commands.h"
#include "cl_demo.h"
#include "features/net-snapshot/capture.h"
#include "features/net-snapshot/entities.h"
#include "features/net-snapshot/ghostactor.h"
#include "cl_main.h"
#include "features/net-snapshot/movers.h"
#include "features/net-snapshot/polyobj.h"
#include "features/net-snapshot/computation/mover_compute.h"
#include "network/servercommands.h"	// the join state command this module fills

// [Kaori-Chani] capture.cpp's, read here so the hook that asks for a census stays one call.
EXTERN_CVAR(Bool, fua_snap_linkcensus)

using namespace zx::netsnapshot;

//*****************************************************************************
//	APPLYING A MOVER RECORD

// Vetoes overridden, not predicates true (9.2), by route: P_ChangeSector's
// (`noblock`) and MoveAttached's (`attached`).
static unsigned int g_moverApplied = 0;
static unsigned int g_moverNoBlock = 0;
static unsigned int g_moverAttached = 0;
static unsigned int g_moverCrushed = 0;
static unsigned int g_moverGone = 0;
// Of those, records with no local mover, ordinarily a joiner's.
static unsigned int g_moverGoneNoLocal = 0;
// Of those, a GONE for a predecessor whose successor this frame already
// put on the plane.
static unsigned int g_moverGoneSuperseded = 0;
// Of the no-local, those whose plane the GONE moved.
static unsigned int g_moverGoneUnheldPlane = 0;
// Planes of an applied GONE refused as older than a newer record there: a
// one-plane successor overtook a two-plane GONE.
static unsigned int g_moverGonePlanesStale = 0;
// Records the sink refused, split so `records` accounts exactly: applied +
// gone + no level + unusable + stale.
static unsigned int g_moverNoLevel = 0;
static unsigned int g_moverUnusable = 0;
// Records from a frame older than one already applied to their plane.
static unsigned int g_moverStale = 0;
// Stale movers destroyed for a live record naming another class or instance.
static unsigned int g_moverEvicted = 0;
// The same for a GONE meeting a predecessor its successor's slot superseded.
static unsigned int g_moverGoneEvicted = 0;
// Of both, a same-class occupant under another generation (v28).
static unsigned int g_moverGenEvicted = 0;
// Tracked occupants destroyed for a legacy waggle; see MoverEvictForWaggle.
static unsigned int g_moverWaggleEvicted = 0;
// The countdown's accuracy: `waitTicks` says the branch ran, and `waitEarly`
// the tics left when a record ended the wait, small when it tracks.
static unsigned int g_moverWaitTicks = 0;
static unsigned int g_moverWaitEnded = 0;
static unsigned int g_moverWaitResid = 0;
static unsigned int g_moverWaitWorst = 0;
static unsigned int g_moverWaitExpired = 0;
// The same for doors, apart: one counter for two producers cannot attribute.
static unsigned int g_moverDoorWaitTicks = 0;
static unsigned int g_moverDoorWaitExpired = 0;
// Records ending a door wait, split at one tic: a re-press ends one early
// on the server as well, which the client cannot tell from drift.
static unsigned int g_moverDoorWaitEnded = 0;
static unsigned int g_moverDoorWaitEarly = 0;
static unsigned int g_moverDoorWaitEarlyWorst = 0;
// Crusher reversals a client derived from its own plane rather than the wire.
static unsigned int g_moverDerivedReversal = 0;
// Records that arrived blocked, and tics the hold broke out of Tick.
static unsigned int g_moverBlockedRecv = 0;
// Legacy handler calls the gate turned away.
static unsigned int g_moverLegacySuppressed = 0;
// Planes taken off the wire from at-rest records, per class, counted when the
// value changes, and the largest correction.
typedef char snapmv_assert_statclasses[
	( SNAPMV_STATCLASSES >= SNAPMV_COUNT ) ? 1 : -1];

static unsigned int g_moverRestPlane[SNAPMV_COUNT] = { 0 };
// Planes taken from a GONE record, apart from the at-rest ones:
// corrected-at-rest and corrected-on-death are different events.
static unsigned int g_moverGonePlane[SNAPMV_COUNT] = { 0 };
static unsigned int g_moverGoneWorst[SNAPMV_COUNT] = { 0 };
static unsigned int g_moverRestWorst[SNAPMV_COUNT] = { 0 };
static unsigned int g_moverHeld = 0;
static unsigned int g_moverHeldOnePlane = 0;
// Planes taken from a blocked record, apart from the at-rest ones.
static unsigned int g_moverStallPlane = 0;
static unsigned int g_moverStallWorst = 0;
// Legacy plane verifications skipped for a plane the snapshot path owns.
static unsigned int g_moverPlaneSuppressed = 0;
// The same, for a plane linked to an owned one.
static unsigned int g_moverLinkSuppressed = 0;
// Legacy mid y offsets skipped on a line attached to an owned plane.
static unsigned int g_moverMidtexSuppressed = 0;

// The client's map reset window (snap_movers.md), and its counts, where
// opened = closed + abandoned + one open.
static bool g_moverResetOpen = false;
static unsigned int g_moverResetEpoch = 0;
static unsigned int g_moverResetsOpened = 0;
static unsigned int g_moverResetsClosed = 0;
static unsigned int g_moverResetsAbandoned = 0;

// The client's reset count, from DestroyAllSectorMovers or SnapJoin, unset
// with the stream epoch.
static BYTE g_moverResetCount = 0;
static bool g_haveResetCount = false;
static unsigned int g_moverResetCountEpoch = 0;
// The datagram's, set by OnSnapshot around its payload's records.
static BYTE g_datagramResetCount = 0;
static bool g_inDatagram = false;

// A record held back, in a reset window, before the reset count is known or
// counted past it, coalesced per plane (snap_movers.md).
struct SnapMoverHeld
{
	int				frame;		// as ApplyingServerTic read it on arrival
	int				arrivalTic;	// the client's gametic then, of the newest record
	WORD			index;
	BYTE			cls, flags, gen, bodyBytes;
	bool			bCounted;	// from a datagram carrying a reset count
	BYTE			resetCount;
	BYTE			body[SNAPMV_BODY_MAX];
};
static TArray<SnapMoverHeld> g_moverHold;
static unsigned int g_moverHoldEpoch = 0;	// the stream epoch its entries belong to
// Per report: deferred, replayed, dropped, coalesced, and the hold's peak.
static unsigned int g_moverHoldIn = 0;
static unsigned int g_moverHoldAhead = 0;
static unsigned int g_moverHoldReplayed = 0;
static unsigned int g_moverHoldDropped = 0;
static unsigned int g_moverHoldCoalesced = 0;
// Held GONEs a newer instance covered in part, narrowed rather than deleted.
static unsigned int g_moverHoldNarrowed = 0;
static unsigned int g_moverHoldPeak = 0;
// Dropped on arrival, counted before the client's reset count.
static unsigned int g_moverBeforeReset = 0;
// Replayed movers ticked forward by their hold: how many, the tics, the
// largest, and holds past the cap, left alone.
static unsigned int g_moverHoldAdvanced = 0;
static unsigned int g_moverHoldAdvanceTics = 0;
static unsigned int g_moverHoldAdvanceWorst = 0;
static unsigned int g_moverHoldAdvanceCapped = 0;
#define SNAPMV_HOLD_ADVANCE_MAX	( 5 * TICRATE )

bool zx::netsnapshot::MoverInReset()
{
	return g_moverResetOpen && ( g_moverResetEpoch == StreamEpoch( ));
}

// A window a reconnect ended, counted once.
static void snapmv_ReapReset()
{
	if ( g_moverResetOpen && ( MoverInReset( ) == false ))
	{
		++g_moverResetsAbandoned;
		g_moverResetOpen = false;
	}
}

// What the hold kept for an earlier stream epoch.
static void snapmv_ReapHold()
{
	const unsigned int epoch = StreamEpoch( );
	if ( g_moverHoldEpoch == epoch )
		return;
	g_moverHoldDropped += g_moverHold.Size( );
	g_moverHold.Clear( );
	g_moverHoldEpoch = epoch;
}

static bool snapmv_HaveResetCount()
{
	return g_haveResetCount && ( g_moverResetCountEpoch == StreamEpoch( ));
}

void zx::netsnapshot::MoverBeginDatagram( BYTE resetCount )
{
	g_datagramResetCount = resetCount;
	g_inDatagram = true;
}

void zx::netsnapshot::MoverEndDatagram()
{
	g_inDatagram = false;
}

void zx::netsnapshot::MoverForgetResetCount()
{
	g_haveResetCount = false;
}

unsigned int zx::netsnapshot::MoverBeforeResetRecords( bool bReset )
{
	const unsigned int n = g_moverBeforeReset;
	if ( bReset )
		g_moverBeforeReset = 0;
	return n;
}

// DestroyAllSectorMovers' loop, shared with the window's opening.
void zx::netsnapshot::DestroySectorMovers()
{
	for ( int i = 0; i < numsectors; i++ )
	{
		// Both channels before either Destroy: a two-plane mover's clears
		// floordata too, which left its CHAN_FLOOR sequence playing.
		if ( sectors[i].ceilingdata )
			SN_StopSequence( &sectors[i], CHAN_CEILING );
		if ( sectors[i].floordata )
			SN_StopSequence( &sectors[i], CHAN_FLOOR );

		if ( sectors[i].ceilingdata )
		{
			sectors[i].ceilingdata->Destroy( );
			sectors[i].ceilingdata = NULL;
		}

		if ( sectors[i].floordata )
		{
			sectors[i].floordata->Destroy( );
			sectors[i].floordata = NULL;
		}
	}

	// And every polyobject action: the server's reset destroys all of its own.
	PolyDestroyAll( );
}

// The movers go as the window opens: the server destroyed its own in
// this reset, and one left ticking until DestroyAllSectorMovers moves the
// plane the reset wrote.
void zx::netsnapshot::MoverResetBegin()
{
	// Without the server's generation - no gate-on join in this stream - this client holds
	// no records of ours and the legacy destroy is the whole reset, as it is today.
	if ( snapmv_HaveResetCount( ) == false )
		return;

	snapmv_ReapReset( );
	if ( MoverInReset( ))
		++g_moverResetsAbandoned;
	g_moverResetOpen = true;
	g_moverResetEpoch = StreamEpoch( );
	++g_moverResetsOpened;
	DestroySectorMovers( );
}

void zx::netsnapshot::MoverResetEnd()
{
	snapmv_ReapReset( );
	if ( MoverInReset( ))
		++g_moverResetsClosed;
	g_moverResetOpen = false;
}

static int snapmv_RecordClassOf( DSectorEffect * pMover );

// The server's mover gate (SnapMoverGate), reset on disconnect.
static bool g_serverGatesMovers = false;

// The frame below which mover records are refused, from before the last
// gate-off (0 none), ignored once the stream epoch moves.
static int g_moverFloor = 0;
static unsigned int g_moverFloorEpoch = 0;
static unsigned int g_gateDropped[2][SNAPMV_COUNT];	// [legacy-made, record-made]
static unsigned int g_gateBelowFloor = 0;
static unsigned int g_gateKeptApplied = 0;

// Whether the snapshot path owns this mover class on this client, so its
// legacy state handlers must not run.
bool zx::netsnapshot::MoverSuppressesLegacy( BYTE cls )
{
	// A server owns its own movers; this asks whether a client's legacy handler must
	// stand aside because records drive this class instead.
	if (( g_serverGatesMovers == false ) || ( NETWORK_InClientMode( ) == false ))
		return false;
	return MoverClassTracked( cls );
}

static bool snapmv_BelowFloor( int frame )
{
	if (( g_moverFloor == 0 ) || ( g_moverFloorEpoch != StreamEpoch( )))
		return false;
	return ( frame - g_moverFloor ) < 0;
}

// A gate-on drops what legacy made and a gate-off what records made, a
// CLIENTSIDE script's mover staying.
static void snapmv_GateDrop( bool bLegacyMade )
{
	TThinkerIterator<DMover> it;
	DMover * p;
	while (( p = it.Next( )) != NULL )
	{
		const int cls = snapmv_RecordClassOf( p );
		if (( cls < 0 ) || ( bLegacyMade ? ( p->m_SnapLegacy == false ) : ( p->m_SnapGen == 0 )))
			continue;
		sector_t * pSec = p->GetSector( );
		if ( pSec->floordata == p )
			SN_StopSequence( pSec, CHAN_FLOOR );
		if ( pSec->ceilingdata == p )
			SN_StopSequence( pSec, CHAN_CEILING );
		p->Destroy( );
		++g_gateDropped[bLegacyMade ? 0 : 1][cls];
	}
	g_gateDropped[bLegacyMade ? 0 : 1][SNAPMV_POLY] += bLegacyMade
		? PolyGateOn( g_gateKeptApplied ) : PolyGateOff( );
}

void zx::netsnapshot::MoverMadeByLegacy( DMover * pMover )
{
	// Only a client has legacy movers to drop; a server's are the originals.
	if (( pMover == NULL ) || ( NETWORK_InClientMode( ) == false ))
		return;
	pMover->m_SnapLegacy = true;
}

// From SnapMoverGate, `frame` 0 leaving the floor.
void zx::netsnapshot::MoverSetServerGate( bool bLegacyOff, int frame )
{
	const unsigned int epoch = StreamEpoch( );
	if ( frame != 0 )
	{
		if (( g_moverFloorEpoch != epoch ) || ( g_moverFloor == 0 ) || (( frame - g_moverFloor ) > 0 ))
			g_moverFloor = frame;
		g_moverFloorEpoch = epoch;
	}
	g_serverGatesMovers = bLegacyOff;
	// Between maps there is nothing to drop, and the map load builds anew.
	if ( gamestate == GS_LEVEL )
		snapmv_GateDrop( bLegacyOff );
}

// At disconnect, the kept polyobject records going with the stream epoch.
void zx::netsnapshot::MoverForgetServerGate( )
{
	g_serverGatesMovers = false;
	g_moverFloor = 0;
}

void zx::netsnapshot::ClientMoverGateStats( SnapMoverGateStats & st, bool bReset )
{
	st.bGate = g_serverGatesMovers;
	st.floor = ( g_moverFloorEpoch == StreamEpoch( )) ? g_moverFloor : 0;
	memset( st.dropped, 0, sizeof( st.dropped ));
	for ( int k = 0; k < 2; ++k )
		for ( int c = 0; c < SNAPMV_COUNT; ++c )
			st.dropped[k][c] = g_gateDropped[k][c];
	st.belowFloor = g_gateBelowFloor;
	st.keptApplied = g_gateKeptApplied;
	if ( bReset )
	{
		memset( g_gateDropped, 0, sizeof( g_gateDropped ));
		g_gateBelowFloor = g_gateKeptApplied = 0;
	}
}

bool zx::netsnapshot::MoverLegacyStandsAside( BYTE cls )
{
	if ( MoverSuppressesLegacy( cls ) == false )
		return false;
	++g_moverLegacySuppressed;
	return true;
}

void zx::netsnapshot::MoverCountOutcome( int outcome )
{
	switch ( outcome )
	{
	case SNAPMV_OUT_APPLIED:	++g_moverApplied;	break;
	case SNAPMV_OUT_GONE:		++g_moverGone;		break;
	case SNAPMV_OUT_UNUSABLE:	++g_moverUnusable;	break;
	case SNAPMV_OUT_STALE:		++g_moverStale;		break;
	default:					break;
	}
}

// The counters live here because the report does; every mover class has to
// reach them, or its zeros are inert rather than correct.
void zx::netsnapshot::MoverCountBlockedRecv()
{
	++g_moverBlockedRecv;
}

void zx::netsnapshot::MoverCountHeld()
{
	++g_moverHeld;
}

void zx::netsnapshot::MoverCountHeldOnePlane()
{
	++g_moverHeldOnePlane;
}

void zx::netsnapshot::MoverCountStallPlane( unsigned int off )
{
	++g_moverStallPlane;
	if ( off > g_moverStallWorst )
		g_moverStallWorst = off;
}

void zx::netsnapshot::MoverCountPlaneSuppressed()
{
	++g_moverPlaneSuppressed;
}

void zx::netsnapshot::MoverCountLinkSuppressed()
{
	++g_moverLinkSuppressed;
}

bool zx::netsnapshot::MoverSkipsPlaneWrite( sector_t *sec, bool bCeiling )
{
	if ( MoverOwnsPlane( sec, bCeiling ))
	{
		++g_moverPlaneSuppressed;
		return true;
	}
	// A plane linked to one a record owns: a late absolute write would undo the record's.
	if ( LinkControlOwned( sec, bCeiling ))
	{
		++g_moverLinkSuppressed;
		return true;
	}
	return false;
}

bool zx::netsnapshot::MoverSkipsMidtexY( line_t *line )
{
	if ( Midtex3dLineOwned( line ) == false )
		return false;
	++g_moverMidtexSuppressed;
	return true;
}

bool zx::netsnapshot::MoverPlaysWaitSound( )
{
	return ( NETWORK_InClientMode( ) == false );
}

bool zx::netsnapshot::MoverClientDerivesReversal( bool bSeen )
{
	if ( ( bSeen == false ) || ( NETWORK_InClientMode( ) == false ) )
		return false;
	++g_moverDerivedReversal;
	return true;
}

bool zx::netsnapshot::MoverClientSkipsWait( bool bSeen )
{
	return NETWORK_InClientMode( ) && ( bSeen == false );
}

void zx::netsnapshot::MoverCountDoorWait( bool bExpired )
{
	if ( NETWORK_InClientMode( ) == false )
		return;
	++g_moverDoorWaitTicks;
	if ( bExpired )
		++g_moverDoorWaitExpired;
}

void zx::netsnapshot::MoverCountDoorWaitEnded( int left )
{
	if ( left <= 1 )
	{
		++g_moverDoorWaitEnded;
		return;
	}
	++g_moverDoorWaitEarly;
	if ( (unsigned int)left > g_moverDoorWaitEarlyWorst )
		g_moverDoorWaitEarlyWorst = (unsigned int)left;
}

void zx::netsnapshot::MoverCountMidtexSuppressed()
{
	++g_moverMidtexSuppressed;
}

// As MoverFloorOwnsPlane, for a plat: its pastdest, stop and reactivate
// verifications otherwise land on a plane its records own.
bool zx::netsnapshot::MoverPlatOwnsPlane( sector_t *sec )
{
	if (( sec == NULL ) || ( MoverSuppressesLegacy( SNAPMV_PLAT ) == false ))
		return false;
	if (( sec->floordata == NULL )
		|| ( sec->floordata->IsKindOf( RUNTIME_CLASS( DPlat )) == false ))
		return false;
	return barrier_cast<DPlat *>( sec->floordata )->m_SnapSeen;
}

bool zx::netsnapshot::MoverOwnsPlane( sector_t *sec, bool bCeiling )
{
	if ( MoverInReset( ))
		return false;
	if ( bCeiling )
		return MoverCeilingOwnsPlane( sec ) || MoverDoorOwnsPlane( sec )
			|| MoverElevatorOwnsPlane( sec ) || MoverPillarOwnsPlane( sec );
	return MoverFloorOwnsPlane( sec ) || MoverPlatOwnsPlane( sec )
		|| MoverElevatorOwnsPlane( sec ) || MoverPillarOwnsPlane( sec );
}

// The correction paths' DMover::MoveAttached, with no veto, the plane being
// written already.
void zx::netsnapshot::MoverMoveAttached( sector_t * pSec, fixed_t move, bool bCeiling, const char * who )
{
	if ( move == 0 )
		return;
	P_Scroll3dMidtex( pSec, 0, move, bCeiling );
	// [Kaori-Chani] The source tree gives P_MoveLinkedSectors a fifth parameter naming who
	// moved the plane, which p_linkedsectors.cpp threads down for one purpose: to reach
	// zx::netsnapshot::TracePlaneWrite. The parameter is a hook's, so it arrives with the
	// hook in step 3 and this calls the four-argument form meanwhile. The argument stays in
	// this function's own signature, where movers.h declares it, so step 3 restores one
	// argument rather than a signature, and the tracing it feeds is diagnostic either way.
	(void)who;
	P_MoveLinkedSectors( pSec, false, move, bCeiling );
}

// Writes the plane a GONE carries, as the server destroyed it, with its
// attached geometry, since nothing after a death corrects a mid-travel plane.
void zx::netsnapshot::MoverApplyGonePlane( sector_t * pSec, int plane, BYTE cls, fixed_t planeD )
{
	if (( pSec == NULL ) || ( cls >= SNAPMV_COUNT ))
		return;

	secplane_t & sp = pSec->GetSecPlane( plane );
	const fixed_t lastpos = sp.d;
	if ( lastpos == planeD )
		return;

	const fixed_t diff = planeD - lastpos;
	const unsigned int off = (unsigned int)(( diff < 0 ) ? -diff : diff );
	++g_moverGonePlane[cls];
	if ( off > g_moverGoneWorst[cls] )
		g_moverGoneWorst[cls] = off;

	sp.d = planeD;
	pSec->ChangePlaneTexZ( plane, sp.HeightDiff( lastpos ));
	if ( plane == sector_t::floor )
	{
		pSec->AdjustFloorClip( );
		MoverMoveAttached( pSec, -diff, false, "gone" );
	}
	else
		MoverMoveAttached( pSec, diff, true, "gone" );
}

void zx::netsnapshot::MoverCountRestPlane( BYTE cls, unsigned int off )
{
	if ( cls >= SNAPMV_COUNT )
		return;
	++g_moverRestPlane[cls];
	if ( off > g_moverRestWorst[cls] )
		g_moverRestWorst[cls] = off;
}

void zx::netsnapshot::MoverCountNoBlock()
{
	++g_moverNoBlock;
}

void zx::netsnapshot::MoverCountAttachedVeto()
{
	++g_moverAttached;
}

void zx::netsnapshot::MoverClientStats( SnapMoverClientStats & st, bool bReset )
{
	st.applied = g_moverApplied;
	st.noblock = g_moverNoBlock;
	st.attached = g_moverAttached;
	st.crushed = g_moverCrushed;
	st.gone = g_moverGone;
	st.goneNoLocal = g_moverGoneNoLocal;
	st.goneUnheldPlane = g_moverGoneUnheldPlane;
	st.gonePlanesStale = g_moverGonePlanesStale;
	st.goneSuperseded = g_moverGoneSuperseded;
	st.noLevel = g_moverNoLevel;
	st.unusable = g_moverUnusable;
	st.evicted = g_moverEvicted;
	st.goneEvicted = g_moverGoneEvicted;
	st.genEvicted = g_moverGenEvicted;
	st.waggleEvicted = g_moverWaggleEvicted;
	st.waitTicks = g_moverWaitTicks;
	st.waitEnded = g_moverWaitEnded;
	st.waitResid = g_moverWaitResid;
	st.waitWorst = g_moverWaitWorst;
	st.waitExpired = g_moverWaitExpired;
	st.doorWaitTicks = g_moverDoorWaitTicks;
	st.doorWaitExpired = g_moverDoorWaitExpired;
	st.doorWaitEnded = g_moverDoorWaitEnded;
	st.doorWaitEarly = g_moverDoorWaitEarly;
	st.doorWaitEarlyWorst = g_moverDoorWaitEarlyWorst;
	st.derivedReversals = g_moverDerivedReversal;
	st.blockedRecv = g_moverBlockedRecv;
	st.legacySuppressed = g_moverLegacySuppressed;
	for ( int i = 0; i < SNAPMV_COUNT; i++ )
	{
		st.restPlane[i] = g_moverRestPlane[i];
		st.gonePlane[i] = g_moverGonePlane[i];
		st.goneWorst[i] = g_moverGoneWorst[i];
		st.restWorst[i] = g_moverRestWorst[i];
	}
	st.held = g_moverHeld;
	st.heldOnePlane = g_moverHeldOnePlane;
	st.stallPlane = g_moverStallPlane;
	st.stallWorst = g_moverStallWorst;
	st.planeSuppressed = g_moverPlaneSuppressed;
	st.linkSuppressed = g_moverLinkSuppressed;
	st.midtexSuppressed = g_moverMidtexSuppressed;
	snapmv_ReapReset( );
	snapmv_ReapHold( );
	st.resetsOpened = g_moverResetsOpened;
	st.resetsClosed = g_moverResetsClosed;
	st.resetsAbandoned = g_moverResetsAbandoned;
	st.bResetOpen = MoverInReset( );
	st.resetCount = g_moverResetCount;
	st.bHaveResetCount = snapmv_HaveResetCount( );
	st.holdIn = g_moverHoldIn;
	st.holdAhead = g_moverHoldAhead;
	st.holdReplayed = g_moverHoldReplayed;
	st.holdDropped = g_moverHoldDropped;
	st.holdCoalesced = g_moverHoldCoalesced;
	st.holdNarrowed = g_moverHoldNarrowed;
	st.holdPeak = g_moverHoldPeak;
	st.holdAdvanced = g_moverHoldAdvanced;
	st.holdAdvanceTics = g_moverHoldAdvanceTics;
	st.holdAdvanceWorst = g_moverHoldAdvanceWorst;
	st.holdAdvanceCapped = g_moverHoldAdvanceCapped;
	st.holdBound = 6 * (unsigned int)numsectors;
	if ( bReset )
	{
		g_moverApplied = g_moverNoBlock = g_moverAttached = 0;
		g_moverCrushed = g_moverGone = g_moverGoneNoLocal = 0;
		g_moverGoneUnheldPlane = g_moverGonePlanesStale = 0;
		g_moverUnusable = g_moverEvicted = g_moverWaitExpired = 0;
		g_moverGoneEvicted = g_moverGenEvicted = g_moverWaggleEvicted = 0;
		g_moverGoneSuperseded = g_moverNoLevel = 0;
		g_moverHoldIn = g_moverHoldAhead = g_moverHoldReplayed = 0;
		g_moverHoldDropped = g_moverHoldCoalesced = g_moverHoldNarrowed = 0;
		g_moverHoldPeak = g_moverHold.Size( );
		g_moverHoldAdvanced = g_moverHoldAdvanceTics = 0;
		g_moverHoldAdvanceWorst = g_moverHoldAdvanceCapped = 0;
		g_moverWaitTicks = g_moverWaitEnded = 0;
		g_moverDoorWaitTicks = g_moverDoorWaitExpired = 0;
		g_moverDoorWaitEnded = g_moverDoorWaitEarly = g_moverDoorWaitEarlyWorst = 0;
		g_moverWaitResid = g_moverWaitWorst = 0;
		g_moverBlockedRecv = g_moverHeld = g_moverLegacySuppressed = 0;
		g_moverHeldOnePlane = 0;
		g_moverStallPlane = g_moverStallWorst = 0;
		g_moverPlaneSuppressed = g_moverLinkSuppressed = 0;
		g_moverMidtexSuppressed = 0;
		memset( g_moverRestPlane, 0, sizeof( g_moverRestPlane ));
		memset( g_moverRestWorst, 0, sizeof( g_moverRestWorst ));
		memset( g_moverGonePlane, 0, sizeof( g_moverGonePlane ));
		memset( g_moverGoneWorst, 0, sizeof( g_moverGoneWorst ));
	}
}

// Applies one plat record, every field but the plane, which is taken on the
// plat's first record and at rest (snap_movers.md).
void DPlat::SnapApply( const SnapMoverPlat & s, BYTE flags )
{
	// A record ending a wait still counting down, sampled before the
	// assignments below overwrite it.
	if ( NETWORK_InClientMode( ) && ( m_Status == waiting )
		&& ( s.status != waiting ) && ( m_Count > 0 ))
	{
		const unsigned int left = (unsigned int)m_Count;
		++g_moverWaitEnded;
		g_moverWaitResid += left;
		if ( left > g_moverWaitWorst )
			g_moverWaitWorst = left;
	}

	m_Speed		= s.speed;
	m_Low		= s.low;
	m_High		= s.high;
	m_Wait		= s.wait;
	m_Count		= s.count;
	m_Tag		= s.tag;
	m_PlatID	= s.platID;
	m_Crush		= s.crush;
	m_Status	= (EPlatState)s.status;
	m_OldStatus	= (EPlatState)s.oldStatus;
	m_Type		= (EPlatType)s.type;

	// Told, not derived (7.1 part 3).
	m_SnapBlocked = (( flags & SNAPMVF_BLOCKED ) != 0 );
	if ( m_SnapBlocked && NETWORK_InClientMode( ))
		MoverCountBlockedRecv( );

	// The plane on the first record and at rest, never while moving (7.1).
	const bool bAtRest = ( (EPlatState)s.status == waiting )
		|| ( (EPlatState)s.status == in_stasis );

	// A stall too: the server's plane holds there, and so will ours, where
	// the client's run had reached when the flag arrived.
	if ((( m_SnapSeen == false ) || bAtRest || m_SnapBlocked ) && ( m_Sector != NULL ))
	{
		// The join check, once per plat: the local plane and texz+d before the
		// write, against the wire's.
		if (( m_SnapSeen == false ) && NETWORK_InClientMode( ))
			Printf( "[mover] join sec %d: wire d %d local d %d | texz+d %d"
				" vbo %d %s | status %d count %d wait %d | blocked %d"
				" | srv tic %d\n",
				(int)( m_Sector - sectors ), (int)s.planeD,
				(int)m_Sector->floorplane.d,
				(int)( m_Sector->GetPlaneTexZ( sector_t::floor )
					+ m_Sector->floorplane.d ),
				(int)m_Sector->vboheight[sector_t::floor],
				m_Sector->VBOHeightcheck( sector_t::floor ) ? "VALID" : "stale",
				(int)s.status, (int)s.count, (int)s.wait,
				m_SnapBlocked ? 1 : 0, NewestGameTic( ));
		const fixed_t lastpos = m_Sector->floorplane.d;
		if (( lastpos != s.planeD ) && m_SnapSeen )
		{
			const fixed_t diff = s.planeD - lastpos;
			const unsigned int off = (unsigned int)(( diff < 0 ) ? -diff : diff );
			if ( bAtRest )
				MoverCountRestPlane( SNAPMV_PLAT, off );
			else
				MoverCountStallPlane( off );
		}
		m_Sector->floorplane.d = s.planeD;
		// The plane's TexZ moves with it by the same delta, as every writer's
		// does, or the drawn height stays where it was (snap_movers.md).
		m_Sector->ChangePlaneTexZ( sector_t::floor,
			m_Sector->floorplane.HeightDiff( lastpos ));
		m_Sector->AdjustFloorClip( );

		// Attached geometry takes every write, first record and at-rest
		// alike, as in DFloor::SnapApply.
		const fixed_t move = s.planeD - lastpos;
		MoverMoveAttached( m_Sector, -move, false,
			m_SnapSeen ? "snap" : "first" );
	}
	m_SnapSeen = true;
	++g_moverApplied;
}

// The last frame applied per plane, by sector, rebuilt as the epoch moves.
static int * g_moverStampFrame[2] = { NULL, NULL };
static int g_moverStampCount = 0;
static unsigned int g_moverStampEpoch = 0;

static int & snapmv_Stamp( int plane, WORD index )
{
	const unsigned int epoch = StreamEpoch( );
	if (( g_moverStampCount != numsectors ) || ( g_moverStampEpoch != epoch ))
	{
		for ( int p = 0; p < 2; ++p )
		{
			delete[] g_moverStampFrame[p];
			g_moverStampFrame[p] = new int[numsectors];
			memset( g_moverStampFrame[p], 0, numsectors * sizeof( int ));
		}
		g_moverStampCount = numsectors;
		g_moverStampEpoch = epoch;
	}
	return g_moverStampFrame[plane][index];
}

// A plane bit of MoverPlanes as the sector's plane index.
static int snapmv_Plane( BYTE bit )
{
	return ( bit == SNAPMV_PLANE_CEILING ) ? sector_t::ceiling : sector_t::floor;
}

// The one map from a local mover's class to its record class, which a new class
// needs a line in (snap_movers.md).
static int snapmv_RecordClassOf( DSectorEffect * pMover )
{
	if ( pMover == NULL )
		return -1;
	if ( pMover->IsKindOf( RUNTIME_CLASS( DPlat )))
		return SNAPMV_PLAT;
	if ( pMover->IsKindOf( RUNTIME_CLASS( DFloor )))
		return SNAPMV_FLOOR;
	if ( pMover->IsKindOf( RUNTIME_CLASS( DCeiling )))
		return SNAPMV_CEILING;
	if ( pMover->IsKindOf( RUNTIME_CLASS( DDoor )))
		return SNAPMV_DOOR;
	if ( pMover->IsKindOf( RUNTIME_CLASS( DElevator )))
		return SNAPMV_ELEVATOR;
	if ( pMover->IsKindOf( RUNTIME_CLASS( DPillar )))
		return SNAPMV_PILLAR;
	return -1;
}

static DSectorEffect * snapmv_Occupant( sector_t * pSec, int plane )
{
	DSectorEffect * pOcc = pSec->floordata;
	if ( plane == sector_t::ceiling )
		pOcc = pSec->ceilingdata;
	return pOcc;
}

// Every record class is a DMover.
static BYTE snapmv_GenOf( DSectorEffect * pMover )
{
	if ( snapmv_RecordClassOf( pMover ) < 0 )
		return 0;
	return static_cast<DMover *>( pMover )->m_SnapGen;
}

// Whether a record applies to this occupant, by class, and by generation unless
// either side has none.
static bool snapmv_IsInstance( DSectorEffect * pOcc, BYTE cls, BYTE gen )
{
	if ( snapmv_RecordClassOf( pOcc ) != cls )
		return false;
	const BYTE occGen = snapmv_GenOf( pOcc );
	return ( gen == 0 ) || ( occGen == 0 ) || ( occGen == gen );
}

// Evicts another class's or instance's occupant of the plane, whose GONE has
// not arrived, leaving its sound for the successor's to replace.
static void snapmv_EvictOccupant( sector_t * pSec, int plane, BYTE cls )
{
	DSectorEffect * pOcc = snapmv_Occupant( pSec, plane );
	if ( pOcc == NULL )
		return;
	++g_moverEvicted;
	// Its own class here means snapmv_IsInstance refused its generation.
	if ( snapmv_RecordClassOf( pOcc ) == cls )
		++g_moverGenEvicted;
	pOcc->Destroy( );
}

// The tracked occupant a GONE ends with its own, while only the snapshot path
// creates that class here; an untracked mover is left.
static DSectorEffect * snapmv_SupersededOccupant( sector_t * pSec, int plane,
	BYTE cls )
{
	DSectorEffect * pOcc = snapmv_Occupant( pSec, plane );
	const int occCls = snapmv_RecordClassOf( pOcc );
	if (( occCls < 0 ) || ( MoverSuppressesLegacy( (BYTE)occCls ) == false ))
		return NULL;

	++g_moverGoneEvicted;
	// As in snapmv_EvictOccupant.
	if ( occCls == cls )
		++g_moverGenEvicted;
	return pOcc;
}

// Ends the tracked occupant of a plane a legacy waggle takes, which has already
// ended on the server.
void zx::netsnapshot::MoverEvictForWaggle( sector_t * pSec, int plane )
{
	if ( pSec == NULL )
		return;
	DSectorEffect * pOcc = snapmv_Occupant( pSec, plane );
	const int occCls = snapmv_RecordClassOf( pOcc );
	if (( occCls < 0 ) || ( MoverSuppressesLegacy( (BYTE)occCls ) == false ))
		return;
	++g_moverWaggleEvicted;
	pOcc->Destroy( );
}

// Writes the plane of a GONE for a mover this client never held, on an empty
// plane only, returning whether it took it.
static bool snapmv_GoneUnheld( sector_t * pSec, int plane, BYTE cls, fixed_t planeD,
	bool bCount = true )
{
	if ( bCount )
		++g_moverGoneNoLocal;
	if (( snapmv_Occupant( pSec, plane ) != NULL ) || ( MoverSuppressesLegacy( cls ) == false ))
		return false;
	if ( pSec->GetSecPlane( plane ).d != planeD )
		++g_moverGoneUnheldPlane;
	MoverApplyGonePlane( pSec, plane, cls, planeD );
	return true;
}

// A two-plane GONE on the planes it applies to, each occupant found destroyed
// after the writes, with no sound stop.
static void snapmv_GoneTwoPlanes( sector_t * pSec, BYTE live, BYTE cls, BYTE gen,
	fixed_t floorD, fixed_t ceilingD )
{
	++g_moverGone;
	DSectorEffect * pDead[2] = { NULL, NULL };
	bool bUnheld = false;
	for ( int k = 0; k < 2; ++k )
	{
		const BYTE b = k ? SNAPMV_PLANE_CEILING : SNAPMV_PLANE_FLOOR;
		if (( live & b ) == 0 )
			continue;
		const int plane = snapmv_Plane( b );
		const fixed_t planeD = k ? ceilingD : floorD;
		DSectorEffect * pOcc = snapmv_Occupant( pSec, plane );
		DSectorEffect * pEnd = snapmv_IsInstance( pOcc, cls, gen ) ? pOcc
			: snapmv_SupersededOccupant( pSec, plane, cls );
		if ( pEnd != NULL )
		{
			MoverApplyGonePlane( pSec, plane, cls, planeD );
			pDead[k] = pEnd;
		}
		else
		{
			// Counted once per record, as one-plane classes count it.
			snapmv_GoneUnheld( pSec, plane, cls, planeD, bUnheld == false );
			bUnheld = true;
		}
	}
	if ( pDead[0] != NULL )
		pDead[0]->Destroy( );
	if (( pDead[1] != NULL ) && ( pDead[1] != pDead[0] ))
		pDead[1]->Destroy( );
}

// A two-plane class's instance holding both planes, or a new one with the
// planes' other occupants evicted.
template<class T> static T * snapmv_TwoPlaneInstance( sector_t * pSec, BYTE cls,
	BYTE gen )
{
	DSectorEffect * pOcc = snapmv_Occupant( pSec, sector_t::floor );
	if ( snapmv_IsInstance( pOcc, cls, gen )
		&& ( snapmv_Occupant( pSec, sector_t::ceiling ) == pOcc ))
		return static_cast<T *>( pOcc );

	// A destroyed occupant reads NULL, so one on both planes goes once.
	snapmv_EvictOccupant( pSec, sector_t::floor, cls );
	snapmv_EvictOccupant( pSec, sector_t::ceiling, cls );
	return new T( pSec );
}

// One elevator record.
static void snapmv_ApplyElevator( sector_t * pSec, BYTE flags, BYTE gen, BYTE live,
	const BYTE * pBody )
{
	SnapMoverElevator es;
	ReadMoverElevator( pBody, es );

	if ( flags & SNAPMVF_GONE )
	{
		snapmv_GoneTwoPlanes( pSec, live, SNAPMV_ELEVATOR, gen, es.floorPlaneD,
			es.ceilingPlaneD );
		return;
	}

	DElevator * pElev = snapmv_TwoPlaneInstance<DElevator>( pSec, SNAPMV_ELEVATOR, gen );
	pElev->SnapApply( es, flags );
	pElev->m_SnapGen = gen;
	pElev->m_SnapLegacy = false;	// adopted: record-owned
	++g_moverApplied;
}

// One pillar record, as the elevator's.
static void snapmv_ApplyPillar( sector_t * pSec, BYTE flags, BYTE gen, BYTE live,
	const BYTE * pBody )
{
	SnapMoverPillar ps;
	ReadMoverPillar( pBody, ps );

	if ( flags & SNAPMVF_GONE )
	{
		snapmv_GoneTwoPlanes( pSec, live, SNAPMV_PILLAR, gen, ps.floorPlaneD,
			ps.ceilingPlaneD );
		return;
	}

	DPillar * pPillar = snapmv_TwoPlaneInstance<DPillar>( pSec, SNAPMV_PILLAR, gen );
	pPillar->SnapApply( ps );
	pPillar->m_SnapGen = gen;
	pPillar->m_SnapLegacy = false;	// adopted: record-owned
	++g_moverApplied;
}

unsigned int zx::netsnapshot::MoverStaleRecords( bool bReset )
{
	const unsigned int n = g_moverStale;
	if ( bReset )
		g_moverStale = 0;
	return n;
}

// Applies one mover record from the sink or the hold's replay, at the record's
// own `frame`, finding the mover on its sector's plane (snap_movers.md).
static void snapmv_ApplyRecord( BYTE cls, BYTE flags, BYTE gen, WORD index,
	const BYTE * pBody, unsigned int bodyBytes, int frame )
{
	// Ahead of everything, the polyobject sink's legacy-live keep included.
	if ( snapmv_BelowFloor( frame ))
	{
		++g_gateBelowFloor;
		return;
	}

	// A polyobject's record goes to its own sink, which re-checks the body.
	if ( cls == SNAPMV_POLY )
	{
		PolyApplyRecord( flags, index, pBody, bodyBytes, frame );
		return;
	}

	// Length is re-checked because cl_entities checking it is not this
	// function's guarantee.
	const unsigned int want = MoverBodyBytes( cls, flags );
	if (( index >= numsectors ) || ( want == 0 ) || ( bodyBytes != want ))
	{
		++g_moverUnusable;
		return;
	}

	// Both NO flags is a retire, which the server never emits.
	const BYTE planes = RecordPlanes( cls, flags );
	if ( planes == 0 )
	{
		++g_moverUnusable;
		return;
	}
	const bool bGone = ( flags & SNAPMVF_GONE ) != 0;

	// The ordering guard by frame against each plane's stamp: a live record is
	// stale if older than any plane it covers, a GONE per plane.
	BYTE live = 0, sameFrame = 0;
	for ( BYTE b = SNAPMV_PLANE_FLOOR; b <= SNAPMV_PLANE_CEILING; b <<= 1 )
	{
		if (( planes & b ) == 0 )
			continue;
		const int stamp = snapmv_Stamp( snapmv_Plane( b ), index );
		if ( frame >= stamp )
			live |= b;
		if ( frame == stamp )
			sameFrame |= b;
	}
	if ( bGone ? ( live == 0 ) : ( live != planes ))
	{
		++g_moverStale;
		return;
	}
	const bool bPartStale = ( live != planes );
	for ( BYTE b = SNAPMV_PLANE_FLOOR; b <= SNAPMV_PLANE_CEILING; b <<= 1 )
	{
		if ( live & b )
			snapmv_Stamp( snapmv_Plane( b ), index ) = frame;
	}

	sector_t * pSec = &sectors[index];

	// A GONE under another generation than this frame's occupant is a
	// predecessor's, which `superseded` counts (expected 0).
	if ( bGone && ( gen != 0 ))
	{
		for ( BYTE b = SNAPMV_PLANE_FLOOR; b <= SNAPMV_PLANE_CEILING; b <<= 1 )
		{
			if (( live & sameFrame & b ) == 0 )
				continue;
			const BYTE occGen = snapmv_GenOf( snapmv_Occupant( pSec, snapmv_Plane( b )));
			if (( occGen != 0 ) && ( occGen != gen ))
				live &= (BYTE)~b;
		}
		if ( live == 0 )
		{
			++g_moverGone;
			++g_moverGoneSuperseded;
			return;
		}
	}
	// After the same-frame test, so a GONE it drops counts only there.
	if ( bPartStale )
		++g_moverGonePlanesStale;

	if ( cls == SNAPMV_ELEVATOR )
	{
		snapmv_ApplyElevator( pSec, flags, gen, live, pBody );
		return;
	}
	if ( cls == SNAPMV_PILLAR )
	{
		snapmv_ApplyPillar( pSec, flags, gen, live, pBody );
		return;
	}

	// One-plane classes from here: the record's one plane.
	const int plane = snapmv_Plane( planes );

	if ( cls == SNAPMV_CEILING )
	{
		DSectorEffect * pOcc = snapmv_Occupant( pSec, plane );
		DCeiling * pCeil = snapmv_IsInstance( pOcc, SNAPMV_CEILING, gen )
			? static_cast<DCeiling *>( pOcc ) : NULL;

		if ( flags & SNAPMVF_GONE )
		{
			++g_moverGone;
			SnapMoverCeiling cg;
			ReadMoverCeiling( pBody, cg );
			DSectorEffect * pDead = pCeil;
			if ( pDead == NULL )
				pDead = snapmv_SupersededOccupant( pSec, plane, cls );
			if ( pDead != NULL )
			{
				MoverApplyGonePlane( pSec, sector_t::ceiling, cls, cg.planeD );

				// The gone flag is the crusher's sound stop, which no state
				// record carries (6.2).
				SN_StopSequence( pSec, CHAN_CEILING );
				pDead->Destroy();
			}
			// Its sound may have started on the reliable stream.
			else if ( snapmv_GoneUnheld( pSec, sector_t::ceiling, cls, cg.planeD ))
				SN_StopSequence( pSec, CHAN_CEILING );
			return;
		}

		SnapMoverCeiling cs;
		ReadMoverCeiling( pBody, cs );

		if ( pCeil == NULL )
		{
			snapmv_EvictOccupant( pSec, plane, cls );
			pCeil = new DCeiling( pSec );
		}
		pCeil->SnapApply( cs, flags );
		pCeil->m_SnapGen = gen;
		pCeil->m_SnapLegacy = false;	// adopted: record-owned
		++g_moverApplied;
		return;
	}

	if ( cls == SNAPMV_DOOR )
	{
		DSectorEffect * pOcc = snapmv_Occupant( pSec, plane );
		DDoor * pDoor = snapmv_IsInstance( pOcc, SNAPMV_DOOR, gen )
			? static_cast<DDoor *>( pOcc ) : NULL;

		if ( flags & SNAPMVF_GONE )
		{
			++g_moverGone;
			SnapMoverDoor dg;
			ReadMoverDoor( pBody, dg );
			DSectorEffect * pDead = pDoor;
			if ( pDead == NULL )
				pDead = snapmv_SupersededOccupant( pSec, plane, cls );
			if ( pDead != NULL )
			{
				MoverApplyGonePlane( pSec, sector_t::ceiling, cls, dg.planeD );
				// No stop, since the server sends StopSectorSequence in order
				// before each Destroy.
				pDead->Destroy();
			}
			else
				snapmv_GoneUnheld( pSec, sector_t::ceiling, cls, dg.planeD );
			return;
		}

		SnapMoverDoor ds;
		ReadMoverDoor( pBody, ds );

		// Off the wire: an end of one of the sector's lines, -1 for none.
		if (( ds.botSpot < -1 ) || ( ds.botSpot >= pSec->linecount * 2 ))
		{
			++g_moverUnusable;
			return;
		}

		if ( pDoor == NULL )
		{
			snapmv_EvictOccupant( pSec, plane, cls );
			pDoor = new DDoor( pSec );
		}
		pDoor->SnapApply( ds, flags );
		pDoor->m_SnapGen = gen;
		pDoor->m_SnapLegacy = false;	// adopted: record-owned
		++g_moverApplied;
		return;
	}

	if ( cls == SNAPMV_FLOOR )
	{
		DSectorEffect * pOcc = snapmv_Occupant( pSec, plane );
		DFloor * pFloor = snapmv_IsInstance( pOcc, SNAPMV_FLOOR, gen )
			? static_cast<DFloor *>( pOcc ) : NULL;

		if ( flags & SNAPMVF_GONE )
		{
			++g_moverGone;
			SnapMoverFloor fg;
			ReadMoverFloor( pBody, fg, ( flags & SNAPMVF_STAIR ) != 0 );
			DSectorEffect * pDead = pFloor;
			if ( pDead == NULL )
				pDead = snapmv_SupersededOccupant( pSec, plane, cls );
			if ( pDead != NULL )
			{
				MoverApplyGonePlane( pSec, sector_t::floor, cls, fg.planeD );

				// No state record can carry a stop (6.2), so the gone flag is
				// that event - as client_DestroyFloor does before destroying.
				SN_StopSequence( pSec, CHAN_FLOOR );
				pDead->Destroy();
			}
			// As the ceiling's.
			else if ( snapmv_GoneUnheld( pSec, sector_t::floor, cls, fg.planeD ))
				SN_StopSequence( pSec, CHAN_FLOOR );
			return;
		}

		SnapMoverFloor fs;
		ReadMoverFloor( pBody, fs, ( flags & SNAPMVF_STAIR ) != 0 );

		if ( pFloor == NULL )
		{
			snapmv_EvictOccupant( pSec, plane, cls );
			pFloor = new DFloor( pSec );
		}
		pFloor->SnapApply( fs, flags );
		pFloor->m_SnapGen = gen;
		pFloor->m_SnapLegacy = false;	// adopted: record-owned
		++g_moverApplied;
		return;
	}

	if ( cls != SNAPMV_PLAT )
	{
		++g_moverUnusable;
		return;
	}

	DSectorEffect * pOcc = snapmv_Occupant( pSec, plane );
	DPlat * pPlat = snapmv_IsInstance( pOcc, SNAPMV_PLAT, gen )
		? static_cast<DPlat *>( pOcc ) : NULL;

	if ( flags & SNAPMVF_GONE )
	{
		// No local plat is ordinary for a lift that completed before this
		// client's first frame, counted (9.2).
		++g_moverGone;
		SnapMoverPlat pg;
		ReadMoverPlat( pBody, pg );
		DSectorEffect * pDead = pPlat;
		if ( pDead == NULL )
			pDead = snapmv_SupersededOccupant( pSec, plane, cls );
		if ( pDead != NULL )
		{
			MoverApplyGonePlane( pSec, sector_t::floor, cls, pg.planeD );
			// As the floor and ceiling branches: the gone flag is the stop, and
			// pDead may be a superseded DFloor still playing its sequence.
			SN_StopSequence( pSec, CHAN_FLOOR );
			pDead->Destroy();
		}
		// As the ceiling's.
		else if ( snapmv_GoneUnheld( pSec, sector_t::floor, cls, pg.planeD ))
			SN_StopSequence( pSec, CHAN_FLOOR );
		return;
	}

	SnapMoverPlat st;
	ReadMoverPlat( pBody, st );

	if ( pPlat == NULL )
	{
		snapmv_EvictOccupant( pSec, plane, cls );
		pPlat = new DPlat( pSec );
	}
	pPlat->SnapApply( st, flags );
	pPlat->m_SnapGen = gen;
	pPlat->m_SnapLegacy = false;	// adopted: record-owned
}

// Into the hold, coalescing per plane, unless a newer held entry has it.
static void snapmv_Hold( BYTE cls, BYTE flags, BYTE gen, WORD index,
	const BYTE * pBody, unsigned int bodyBytes, int frame )
{
	snapmv_ReapHold( );
	++g_moverHoldIn;
	const BYTE planes = RecordPlanes( cls, flags );
	const bool bGone = ( flags & SNAPMVF_GONE ) != 0;
	BYTE keepNo = 0;
	int match = -1;
	// A polyobject's record is its whole state, so one entry each, the newest,
	// matched within its class.
	if ( cls == SNAPMV_POLY )
	{
		for ( unsigned int i = 0; i < g_moverHold.Size( ); )
		{
			const SnapMoverHeld & h = g_moverHold[i];
			if (( h.cls != SNAPMV_POLY ) || ( h.index != index ))
			{
				++i;
				continue;
			}
			++g_moverHoldCoalesced;
			if ( h.frame > frame )
				return;
			g_moverHold.Delete( i );
		}
	}
	else if (( planes != 0 ) && ( index < numsectors ))
	{
		// As the sink would refuse it on replay: a live record if any plane it
		// covers holds a newer entry, a GONE only if every one does.
		BYTE newer = 0;
		for ( unsigned int i = 0; i < g_moverHold.Size( ); ++i )
		{
			const SnapMoverHeld & h = g_moverHold[i];
			if (( h.index == index ) && ( h.frame > frame ))
				newer |= (BYTE)( RecordPlanes( h.cls, h.flags ) & planes );
		}
		if ( bGone ? ( newer == planes ) : ( newer != 0 ))
		{
			++g_moverHoldCoalesced;
			return;
		}
		for ( unsigned int i = 0; i < g_moverHold.Size( ); )
		{
			SnapMoverHeld & h = g_moverHold[i];
			const BYTE held = RecordPlanes( h.cls, h.flags );
			if (( h.index != index ) || (( held & planes ) == 0 ))
			{
				++i;
				continue;
			}
			// A GONE never replaces the live entry it ends: that GONE writes
			// its plane only over a mover.
			if (( h.gen == gen ) && ( h.cls == cls )
				&& ((( h.flags & SNAPMVF_GONE ) != 0 ) == bGone ))
			{
				match = (int)i;
				keepNo = (BYTE)( h.flags & ( SNAPMVF_NOFLOOR | SNAPMVF_NOCEILING ));
			}
			else if (( bGone == false ) && ( h.gen != gen ) && ( h.frame < frame ))
			{
				// A newer instance replaces entries whose planes it covers, and
				// narrows a GONE it covers in part.
				if (( held & ~planes ) == 0 )
				{
					g_moverHold.Delete( i );
					++g_moverHoldCoalesced;
					continue;
				}
				if ( h.flags & SNAPMVF_GONE )
				{
					h.flags |= ComputeMoverNoFlags( (BYTE)( held & planes ));
					++g_moverHoldNarrowed;
				}
			}
			++i;
		}
	}

	if ( match >= 0 )
		++g_moverHoldCoalesced;
	else
	{
		match = (int)g_moverHold.Reserve( 1 );
		if ( g_moverHold.Size( ) > g_moverHoldPeak )
			g_moverHoldPeak = g_moverHold.Size( );
	}
	SnapMoverHeld & h = g_moverHold[match];
	h.frame = frame;
	h.arrivalTic = gametic;
	h.index = index;
	h.cls = cls;
	h.flags = bGone ? (BYTE)( flags | keepNo ) : flags;
	h.gen = gen;
	h.bodyBytes = (BYTE)bodyBytes;
	h.bCounted = g_inDatagram;
	h.resetCount = g_datagramResetCount;
	memcpy( h.body, pBody, bodyBytes );
}

// The sink entities.cpp routes an arriving SNAPEV_MOVER to.
static void snapmv_MoverSink( BYTE cls, BYTE flags, BYTE gen, WORD index,
	const BYTE * pBody, unsigned int bodyBytes )
{
	// Reconnecting or between maps: numsectors is whatever the teardown
	// left.
	if ( gamestate != GS_LEVEL )
	{
		++g_moverNoLevel;
		return;
	}

	const int frame = ApplyingServerTic( );
	if ( bodyBytes > SNAPMV_BODY_MAX )
	{
		snapmv_ApplyRecord( cls, flags, gen, index, pBody, bodyBytes, frame );
		return;		// counted unusable there
	}

	const bool bHave = snapmv_HaveResetCount( );
	if ( g_inDatagram && bHave )
	{
		const int ahead = (SBYTE)(BYTE)( g_datagramResetCount - g_moverResetCount );
		if ( ahead < 0 )
		{
			++g_moverBeforeReset;
			return;
		}
		if ( ahead > 0 )
		{
			++g_moverHoldAhead;
			snapmv_Hold( cls, flags, gen, index, pBody, bodyBytes, frame );
			return;
		}
	}
	if ( MoverInReset( ) || ( g_inDatagram && ( bHave == false )))
	{
		snapmv_Hold( cls, flags, gen, index, pBody, bodyBytes, frame );
		return;
	}
	snapmv_ApplyRecord( cls, flags, gen, index, pBody, bodyBytes, frame );
}

// The mover a replayed sector entry made or updated, if it still follows
// that entry.
static DMover * snapmv_ReplayedMover( const SnapMoverHeld & h )
{
	if ( h.index >= numsectors )
		return NULL;
	const BYTE planes = RecordPlanes( h.cls, h.flags );
	sector_t * pSec = &sectors[h.index];
	DSectorEffect * pOcc = NULL;
	if ( planes & SNAPMV_PLANE_FLOOR )
		pOcc = snapmv_Occupant( pSec, sector_t::floor );
	else if ( planes & SNAPMV_PLANE_CEILING )
		pOcc = snapmv_Occupant( pSec, sector_t::ceiling );
	if (( snapmv_RecordClassOf( pOcc ) != h.cls ) || ( snapmv_GenOf( pOcc ) != h.gen ))
		return NULL;
	return static_cast<DMover *>( pOcc );
}

// Ticks each replayed mover forward by its own Tick, once per thinker, for the
// gametics since its newest record arrived, but not after a GONE or a blocked
// record, since a replayed record starts its mover late by the hold.
static void snapmv_AdvanceReplayed( const TArray<SnapMoverHeld> & replayed )
{
	struct Todo { DMover * pMover; int poly; int tics; };
	TArray<Todo> todo;
	for ( unsigned int i = 0; i < replayed.Size( ); ++i )
	{
		const SnapMoverHeld & h = replayed[i];
		Todo t = { NULL, -1, 0 };
		if ( h.cls == SNAPMV_POLY )
			t.poly = h.index;
		else if (( t.pMover = snapmv_ReplayedMover( h )) == NULL )
			continue;
		if (( h.flags & ( SNAPMVF_GONE | SNAPMVF_BLOCKED )) == 0 )
			t.tics = gametic - h.arrivalTic;
		unsigned int k = 0;
		while (( k < todo.Size( )) && (( todo[k].pMover != t.pMover ) || ( todo[k].poly != t.poly )))
			++k;
		if ( k == todo.Size( ))
			todo.Push( t );
		else
			todo[k] = t;
	}

	for ( unsigned int k = 0; k < todo.Size( ); ++k )
	{
		const int tics = todo[k].tics;
		if ( tics <= 0 )
			continue;
		if ( tics > SNAPMV_HOLD_ADVANCE_MAX )
		{
			++g_moverHoldAdvanceCapped;
			continue;
		}
		// A polyobject counts once, and only if it had a live action left.
		if ( todo[k].poly >= 0 )
		{
			if ( PolyAdvance( (WORD)todo[k].poly, tics ) == 0 )
				continue;
		}
		else
		{
			DMover * p = todo[k].pMover;
			for ( int n = 0; ( n < tics ) && (( p->ObjectFlags & OF_EuthanizeMe ) == 0 ); ++n )
				p->Tick( );
		}
		++g_moverHoldAdvanced;
		g_moverHoldAdvanceTics += tics;
		if ( (unsigned int)tics > g_moverHoldAdvanceWorst )
			g_moverHoldAdvanceWorst = tics;
	}
}

// From DestroyAllSectorMovers after its destroy loop, and from SnapJoin:
// replays the hold's entries for this count in arrival order, none while a
// window is open, drops older ones, keeps later ones, and advances the
// replayed.
void zx::netsnapshot::MoverSetResetCount( BYTE count )
{
	snapmv_ReapHold( );
	g_moverResetCount = count;
	g_haveResetCount = true;
	g_moverResetCountEpoch = StreamEpoch( );
	if ( MoverInReset( ))
		return;

	unsigned int kept = 0;
	TArray<SnapMoverHeld> replayed;
	for ( unsigned int i = 0; i < g_moverHold.Size( ); ++i )
	{
		const SnapMoverHeld h = g_moverHold[i];
		if ( h.bCounted )
		{
			const int ahead = (SBYTE)(BYTE)( h.resetCount - count );
			if ( ahead < 0 )
			{
				++g_moverHoldDropped;
				continue;
			}
			if ( ahead > 0 )
			{
				g_moverHold[kept++] = h;
				continue;
			}
		}
		++g_moverHoldReplayed;
		snapmv_ApplyRecord( h.cls, h.flags, h.gen, h.index, h.body,
			h.bodyBytes, h.frame );
		replayed.Push( h );
	}
	g_moverHold.Resize( kept );
	snapmv_AdvanceReplayed( replayed );
}

void zx::netsnapshot::MoverInstallSinks()
{
	static bool s_installed = false;
	if ( s_installed )
		return;
	SetMoverSink( snapmv_MoverSink );
	s_installed = true;
}

// Hands this plat's state, DPlat::Serialize's fields and the plane, to the
// capture table.
void DPlat::SnapCapture( bool bGone )
{
	if ( m_Sector == NULL )
		return;

	// Zeroed first, since the dirty test is a memcmp over the padding too.
	SnapMoverPlat s;
	memset( &s, 0, sizeof( s ));
	s.speed		= m_Speed;
	s.low		= m_Low;
	s.high		= m_High;
	// floorplane: a plat moves the floor through MoveFloor, and Tick's
	// floorOrCeiling test picks which plane to VERIFY, not which one it drives.
	s.planeD	= m_Sector->floorplane.d;
	s.wait		= (SWORD)m_Wait;
	// 0 outside `waiting`, where it may be uninitialised.
	s.count		= ( m_Status == waiting ) ? (SWORD)m_Count : 0;
	s.tag		= (SWORD)m_Tag;
	s.platID	= (SWORD)m_PlatID;
	s.crush		= (SWORD)m_Crush;
	s.status	= (BYTE)m_Status;
	s.oldStatus	= (BYTE)m_OldStatus;
	s.type		= (BYTE)m_Type;

	MoverCapturePlat( this, (WORD)( m_Sector - sectors ), s, m_SnapBlocked,
		bGone );
}

// DFloor::Serialize's list less m_Texture and m_NewSpecial, the stair half only
// for the two types that read it.
void DFloor::SnapCapture( bool bGone )
{
	if ( m_Sector == NULL )
		return;

	const bool bStair = ( m_Type == buildStair ) || ( m_Type == waitStair );

	SnapMoverFloor s;
	memset( &s, 0, sizeof( s ));	// padding; see DPlat::SnapCapture
	s.speed			= m_Speed;
	s.destDist		= m_FloorDestDist;
	s.planeD		= m_Sector->floorplane.d;
	s.floorID		= (SWORD)m_FloorID;
	s.crush			= (SWORD)m_Crush;
	s.type			= (BYTE)m_Type;
	s.hexencrush	= m_Hexencrush ? 1 : 0;
	s.direction		= (SBYTE)m_Direction;
	if ( bStair )
	{
		s.orgDist		= m_OrgDist;
		s.resetCount	= m_ResetCount;
		s.delay			= m_Delay;
		s.pauseTime		= m_PauseTime;
		s.stepTime		= m_StepTime;
		s.perStepTime	= m_PerStepTime;
	}

	MoverCaptureFloor( this, (WORD)( m_Sector - sectors ), s, bStair,
		m_SnapBlocked, bGone );
}

// Apply one record.
void DFloor::SnapApply( const SnapMoverFloor & s, BYTE flags )
{
	m_Speed			= s.speed;
	m_FloorDestDist	= s.destDist;
	m_FloorID		= s.floorID;
	m_Crush			= s.crush;
	m_Type			= (EFloor)s.type;
	m_Hexencrush	= ( s.hexencrush != 0 );
	m_Direction		= s.direction;
	if ( flags & SNAPMVF_STAIR )
	{
		// [Kaori-Chani] m_OrgDist is an int holding a 16.16 value -- this tree's own choice,
		// not ours -- and s.orgDist is a fixed_t off the wire, so the raw is what it wants.
		m_OrgDist		= (int)s.orgDist;
		m_ResetCount	= s.resetCount;
		m_Delay			= s.delay;
		m_PauseTime		= s.pauseTime;
		m_StepTime		= s.stepTime;
		m_PerStepTime	= s.perStepTime;
	}

	m_SnapBlocked = (( flags & SNAPMVF_BLOCKED ) != 0 );
	if ( m_SnapBlocked && NETWORK_InClientMode( ))
		MoverCountBlockedRecv( );

	// The plane on the first record, at rest (EV_FloorCrushStop's included)
	// or stalled, never while moving.
	if ((( m_SnapSeen == false ) || ( s.direction == 0 ) || m_SnapBlocked )
		&& ( m_Sector != NULL ))
	{
		const fixed_t lastpos = m_Sector->floorplane.d;
		const fixed_t move = s.planeD - lastpos;
		if (( move != 0 ) && m_SnapSeen )
		{
			const unsigned int off = (unsigned int)(( move < 0 ) ? -move : move );
			if ( s.direction == 0 )
				MoverCountRestPlane( SNAPMV_FLOOR, off );
			else
				MoverCountStallPlane( off );
		}
		m_Sector->floorplane.d = s.planeD;
		m_Sector->ChangePlaneTexZ( sector_t::floor,
			m_Sector->floorplane.HeightDiff( lastpos ));
		m_Sector->AdjustFloorClip( );

		// Attached geometry takes every write by the same delta, the first
		// record's included, as later corrections are relative.
		MoverMoveAttached( m_Sector, -move, false,
			m_SnapSeen ? "snap" : "first" );
	}
	m_SnapSeen = true;
}

// Whether a floor's record has made this plane the snapshot path's, so a legacy
// verification is skipped.
bool zx::netsnapshot::MoverFloorOwnsPlane( sector_t *sec )
{
	if (( sec == NULL ) || ( MoverSuppressesLegacy( SNAPMV_FLOOR ) == false ))
		return false;
	if (( sec->floordata == NULL )
		|| ( sec->floordata->IsKindOf( RUNTIME_CLASS( DFloor )) == false ))
		return false;
	return barrier_cast<DFloor *>( sec->floordata )->m_SnapSeen;
}

// DElevator::Serialize's list less the interpolations, plus
// both planes; see SnapMoverElevator.
void DElevator::SnapCapture( bool bGone )
{
	if ( m_Sector == NULL )
		return;

	SnapMoverElevator s;
	memset( &s, 0, sizeof( s ));	// padding; see DPlat::SnapCapture
	s.speed				= m_Speed;
	s.floorDestDist		= m_FloorDestDist;
	s.ceilingDestDist	= m_CeilingDestDist;
	s.floorPlaneD		= m_Sector->floorplane.d;
	s.ceilingPlaneD		= m_Sector->ceilingplane.d;
	s.elevatorID		= (SWORD)m_ElevatorID;
	s.type				= (BYTE)m_Type;
	s.direction			= (SBYTE)m_Direction;

	MoverCaptureElevator( this, (WORD)( m_Sector - sectors ), s,
		m_SnapBlocked, bGone );
}

// Applies one record, only the first and a stall writing the planes, as there
// is no rest state.
void DElevator::SnapApply( const SnapMoverElevator & s, BYTE flags )
{
	m_Speed				= s.speed;
	m_FloorDestDist		= s.floorDestDist;
	m_CeilingDestDist	= s.ceilingDestDist;
	m_ElevatorID		= s.elevatorID;
	m_Type				= (EElevator)s.type;
	m_Direction			= s.direction;

	m_SnapBlocked = (( flags & SNAPMVF_BLOCKED ) != 0 );
	if ( m_SnapBlocked && NETWORK_InClientMode( ))
		MoverCountBlockedRecv( );

	// The first record, or a stall, as DFloor's, for each plane with its
	// attached geometry.
	if ((( m_SnapSeen == false ) || m_SnapBlocked ) && ( m_Sector != NULL ))
	{
		const fixed_t floorMove = s.floorPlaneD - m_Sector->floorplane.d;
		const fixed_t ceilingMove = s.ceilingPlaneD - m_Sector->ceilingplane.d;
		if ( m_SnapSeen )
		{
			if ( floorMove != 0 )
				MoverCountStallPlane(
					(unsigned int)(( floorMove < 0 ) ? -floorMove : floorMove ));
			if ( ceilingMove != 0 )
				MoverCountStallPlane(
					(unsigned int)(( ceilingMove < 0 ) ? -ceilingMove : ceilingMove ));
		}
		const fixed_t lastFloor = m_Sector->floorplane.d;
		m_Sector->floorplane.d = s.floorPlaneD;
		m_Sector->ChangePlaneTexZ( sector_t::floor,
			m_Sector->floorplane.HeightDiff( lastFloor ));
		m_Sector->AdjustFloorClip( );
		MoverMoveAttached( m_Sector, -floorMove, false,
			m_SnapSeen ? "snap" : "first" );

		const fixed_t lastCeiling = m_Sector->ceilingplane.d;
		m_Sector->ceilingplane.d = s.ceilingPlaneD;
		m_Sector->ChangePlaneTexZ( sector_t::ceiling,
			m_Sector->ceilingplane.HeightDiff( lastCeiling ));
		MoverMoveAttached( m_Sector, ceilingMove, true,
			m_SnapSeen ? "snap" : "first" );
	}
	m_SnapSeen = true;
}

// As MoverFloorOwnsPlane, for both of an elevator's planes.
bool zx::netsnapshot::MoverElevatorOwnsPlane( sector_t *sec )
{
	if (( sec == NULL ) || ( MoverSuppressesLegacy( SNAPMV_ELEVATOR ) == false ))
		return false;
	if (( sec->floordata == NULL )
		|| ( sec->floordata->IsKindOf( RUNTIME_CLASS( DElevator )) == false ))
		return false;
	return barrier_cast<DElevator *>( sec->floordata )->m_SnapSeen;
}

// DCeiling::Serialize's list less m_Texture and m_NewSpecial, plus the plane.
void DCeiling::SnapCapture( bool bGone )
{
	if ( m_Sector == NULL )
		return;

	// Zeroed first; see DPlat::SnapCapture.
	SnapMoverCeiling s;
	memset( &s, 0, sizeof( s ));
	s.speed			= m_Speed;
	s.speed1		= m_Speed1;
	s.speed2		= m_Speed2;
	s.bottom		= m_BottomHeight;
	s.top			= m_TopHeight;
	// The ceiling plane, which DCeiling always drives through MoveCeiling.
	s.planeD		= m_Sector->ceilingplane.d;
	s.tag			= (SWORD)m_Tag;
	s.ceilingID		= (SWORD)m_CeilingID;
	s.crush			= (SWORD)m_Crush;
	s.type			= (BYTE)m_Type;
	s.silent		= (BYTE)m_Silent;
	s.hexencrush	= m_Hexencrush ? 1 : 0;
	s.direction		= (SBYTE)m_Direction;
	s.oldDirection	= (SBYTE)m_OldDirection;

	MoverCaptureCeiling( this, (WORD)( m_Sector - sectors ), s,
		m_SnapBlocked, bGone );
}

// Apply one record.
void DCeiling::SnapApply( const SnapMoverCeiling & s, BYTE flags )
{
	m_Speed			= s.speed;
	m_Speed1		= s.speed1;
	m_Speed2		= s.speed2;
	m_BottomHeight	= s.bottom;
	m_TopHeight		= s.top;
	m_Tag			= s.tag;
	m_CeilingID		= s.ceilingID;
	m_Crush			= s.crush;
	m_Type			= (ECeiling)s.type;
	m_Silent		= s.silent;
	m_Hexencrush	= ( s.hexencrush != 0 );
	m_Direction		= s.direction;
	m_OldDirection	= s.oldDirection;

	m_SnapBlocked = (( flags & SNAPMVF_BLOCKED ) != 0 );
	if ( m_SnapBlocked && NETWORK_InClientMode( ))
		MoverCountBlockedRecv( );

	// The plane on the first record, in stasis (Ceiling_Stop's included) or
	// stalled, never while moving.
	if ((( m_SnapSeen == false ) || ( s.direction == 0 ) || m_SnapBlocked )
		&& ( m_Sector != NULL ))
	{
		const fixed_t lastpos = m_Sector->ceilingplane.d;
		const fixed_t move = s.planeD - lastpos;
		if (( move != 0 ) && m_SnapSeen )
		{
			const unsigned int off = (unsigned int)(( move < 0 ) ? -move : move );
			if ( s.direction == 0 )
				MoverCountRestPlane( SNAPMV_CEILING, off );
			else
				MoverCountStallPlane( off );
		}
		m_Sector->ceilingplane.d = s.planeD;
		// A plane and its TexZ are a pair; every other writer moves both.
		m_Sector->ChangePlaneTexZ( sector_t::ceiling,
			m_Sector->ceilingplane.HeightDiff( lastpos ));

		// Attached geometry takes every write, first record included, as in
		// DFloor::SnapApply.
		MoverMoveAttached( m_Sector, move, true,
			m_SnapSeen ? "snap" : "first" );
	}
	m_SnapSeen = true;
}

// Whether a ceiling's record has made this plane the snapshot path's, so a
// legacy verification is skipped.
bool zx::netsnapshot::MoverCeilingOwnsPlane( sector_t *sec )
{
	if (( sec == NULL ) || ( MoverSuppressesLegacy( SNAPMV_CEILING ) == false ))
		return false;
	if (( sec->ceilingdata == NULL )
		|| ( sec->ceilingdata->IsKindOf( RUNTIME_CLASS( DCeiling )) == false ))
		return false;
	return barrier_cast<DCeiling *>( sec->ceilingdata )->m_SnapSeen;
}

// m_BotSpot as an end of one of the sector's lines (line slot * 2, plus 1 for
// v2), -1 for none or a spot no line of it ends at.
static SDWORD snapdoor_EncodeSpot( const sector_t * sec, const vertex_t * v )
{
	if ( v == NULL )
		return -1;
	for ( int i = 0; i < sec->linecount; i++ )
	{
		if ( sec->lines[i]->v1 == v )
			return i * 2;
		if ( sec->lines[i]->v2 == v )
			return i * 2 + 1;
	}
	return -1;
}

// The sink has range-checked `spot`.
static vertex_t * snapdoor_DecodeSpot( const sector_t * sec, SDWORD spot )
{
	if ( spot < 0 )
		return NULL;
	const line_t * line = sec->lines[spot / 2];
	return ( spot & 1 ) ? line->v2 : line->v1;
}

// DDoor::Serialize's field list plus the plane; see
// SnapMoverDoor.
void DDoor::SnapCapture( bool bGone )
{
	if ( m_Sector == NULL )
		return;

	// Zeroed first; see DPlat::SnapCapture.
	SnapMoverDoor s;
	memset( &s, 0, sizeof( s ));
	s.speed			= m_Speed;
	s.topDist		= m_TopDist;
	s.botDist		= m_BotDist;
	s.oldFloorDist	= m_OldFloorDist;
	s.planeD		= m_Sector->ceilingplane.d;
	s.topWait		= m_TopWait;
	// Live only while waiting; elsewhere it holds what the last wait left.
	s.topCountdown	= (( m_Direction == 0 ) || ( m_Direction == 2 )) ? m_TopCountdown : 0;
	s.botSpot		= snapdoor_EncodeSpot( m_Sector, m_BotSpot );
	s.lightTag		= (SWORD)m_LightTag;
	s.doorID		= (SWORD)m_DoorID;
	s.type			= (BYTE)m_Type;
	s.direction		= (SBYTE)m_Direction;

	MoverCaptureDoor( this, (WORD)( m_Sector - sectors ), s,
		m_SnapBlocked, bGone );
}

// Apply one record.
void DDoor::SnapApply( const SnapMoverDoor & s, BYTE flags )
{
	// A record ending a wait the client was still counting, sampled before the
	// assignments overwrite it: the local countdown's accuracy, as DPlat's.
	if ( NETWORK_InClientMode( ) && m_SnapSeen
		&& (( m_Direction == 0 ) || ( m_Direction == 2 ))
		&& ( s.direction != 0 ) && ( s.direction != 2 ) && ( m_TopCountdown > 0 ))
		MoverCountDoorWaitEnded( m_TopCountdown );

	m_Speed			= s.speed;
	m_TopDist		= s.topDist;
	m_BotDist		= s.botDist;
	m_OldFloorDist	= s.oldFloorDist;
	m_TopWait		= s.topWait;
	m_TopCountdown	= s.topCountdown;
	m_BotSpot		= snapdoor_DecodeSpot( m_Sector, s.botSpot );
	m_LightTag		= s.lightTag;
	m_DoorID		= s.doorID;
	m_Type			= (EVlDoor)s.type;
	m_Direction		= s.direction;

	m_SnapBlocked = (( flags & SNAPMVF_BLOCKED ) != 0 );
	if ( m_SnapBlocked && NETWORK_InClientMode( ))
		MoverCountBlockedRecv( );

	// The plane on the first record, at rest (0 or 2) or stalled, never moving.
	const bool bAtRest = ( s.direction == 0 ) || ( s.direction == 2 );
	if ((( m_SnapSeen == false ) || bAtRest || m_SnapBlocked ) && ( m_Sector != NULL ))
	{
		const fixed_t lastpos = m_Sector->ceilingplane.d;
		const fixed_t move = s.planeD - lastpos;
		if (( move != 0 ) && m_SnapSeen )
		{
			const unsigned int off = (unsigned int)(( move < 0 ) ? -move : move );
			if ( bAtRest )
				MoverCountRestPlane( SNAPMV_DOOR, off );
			else
				MoverCountStallPlane( off );
		}
		m_Sector->ceilingplane.d = s.planeD;
		m_Sector->ChangePlaneTexZ( sector_t::ceiling,
			m_Sector->ceilingplane.HeightDiff( lastpos ));
		MoverMoveAttached( m_Sector, move, true,
			m_SnapSeen ? "snap" : "first" );
	}
	m_SnapSeen = true;
}

// As MoverCeilingOwnsPlane, for a door.
bool zx::netsnapshot::MoverDoorOwnsPlane( sector_t *sec )
{
	if (( sec == NULL ) || ( MoverSuppressesLegacy( SNAPMV_DOOR ) == false ))
		return false;
	if (( sec->ceilingdata == NULL )
		|| ( sec->ceilingdata->IsKindOf( RUNTIME_CLASS( DDoor )) == false ))
		return false;
	return barrier_cast<DDoor *>( sec->ceilingdata )->m_SnapSeen;
}

// DPillar::Serialize's list less the interpolations, plus both
// planes and the stalled ones; see SnapMoverPillar.
void DPillar::SnapCapture( bool bGone )
{
	if ( m_Sector == NULL )
		return;

	SnapMoverPillar s;
	memset( &s, 0, sizeof( s ));	// padding; see DPlat::SnapCapture
	s.floorSpeed	= m_FloorSpeed;
	s.ceilingSpeed	= m_CeilingSpeed;
	s.floorTarget	= m_FloorTarget;
	s.ceilingTarget	= m_CeilingTarget;
	s.floorPlaneD	= m_Sector->floorplane.d;
	s.ceilingPlaneD	= m_Sector->ceilingplane.d;
	s.pillarID		= (SWORD)m_PillarID;
	s.crush			= (SWORD)m_Crush;
	s.type			= (BYTE)m_Type;
	s.hexencrush	= m_Hexencrush ? 1 : 0;
	s.blocked		= m_SnapBlocked;

	MoverCapturePillar( this, (WORD)( m_Sector - sectors ), s, bGone );
}

// Applies one record, both planes on the first and after that only a stalled
// one, a moving plane being the client's to run.
void DPillar::SnapApply( const SnapMoverPillar & s )
{
	m_Type			= (EPillar)s.type;
	m_FloorSpeed	= s.floorSpeed;
	m_CeilingSpeed	= s.ceilingSpeed;
	m_FloorTarget	= s.floorTarget;
	m_CeilingTarget	= s.ceilingTarget;
	m_Crush			= s.crush;
	m_Hexencrush	= ( s.hexencrush != 0 );
	m_PillarID		= s.pillarID;

	m_SnapBlocked = (BYTE)( s.blocked & SNAPMV_PLANE_BOTH );
	if ( m_SnapBlocked && NETWORK_InClientMode( ))
		MoverCountBlockedRecv( );

	const BYTE write = m_SnapSeen ? m_SnapBlocked : (BYTE)SNAPMV_PLANE_BOTH;
	if (( write & SNAPMV_PLANE_FLOOR ) && ( m_Sector != NULL ))
	{
		const fixed_t move = s.floorPlaneD - m_Sector->floorplane.d;
		if ( m_SnapSeen && ( move != 0 ))
			MoverCountStallPlane( (unsigned int)(( move < 0 ) ? -move : move ));
		const fixed_t lastpos = m_Sector->floorplane.d;
		m_Sector->floorplane.d = s.floorPlaneD;
		m_Sector->ChangePlaneTexZ( sector_t::floor,
			m_Sector->floorplane.HeightDiff( lastpos ));
		m_Sector->AdjustFloorClip( );
		MoverMoveAttached( m_Sector, -move, false, m_SnapSeen ? "snap" : "first" );
	}
	if (( write & SNAPMV_PLANE_CEILING ) && ( m_Sector != NULL ))
	{
		const fixed_t move = s.ceilingPlaneD - m_Sector->ceilingplane.d;
		if ( m_SnapSeen && ( move != 0 ))
			MoverCountStallPlane( (unsigned int)(( move < 0 ) ? -move : move ));
		const fixed_t lastpos = m_Sector->ceilingplane.d;
		m_Sector->ceilingplane.d = s.ceilingPlaneD;
		m_Sector->ChangePlaneTexZ( sector_t::ceiling,
			m_Sector->ceilingplane.HeightDiff( lastpos ));
		MoverMoveAttached( m_Sector, move, true, m_SnapSeen ? "snap" : "first" );
	}
	m_SnapSeen = true;
}

// As MoverElevatorOwnsPlane.
bool zx::netsnapshot::MoverPillarOwnsPlane( sector_t *sec )
{
	if (( sec == NULL ) || ( MoverSuppressesLegacy( SNAPMV_PILLAR ) == false ))
		return false;
	if (( sec->floordata == NULL )
		|| ( sec->floordata->IsKindOf( RUNTIME_CLASS( DPillar )) == false ))
		return false;
	return barrier_cast<DPillar *>( sec->floordata )->m_SnapSeen;
}

//============================================================================
//
// Midtex3dLineOwned
//
// Whether this line is attached to a snapshot-owned plane, ownership first.
//
//============================================================================

bool zx::netsnapshot::Midtex3dLineOwned(line_t *line)
{
	for (int i = 0; i < numsectors; i++)
	{
		for (int p = 0; p < 2; p++)
		{
			if (MoverOwnsPlane(&sectors[i], p != 0) == false)
				continue;
			extsector_t::midtex::plane &mp = p ? sectors[i].e->Midtex.Ceiling
				: sectors[i].e->Midtex.Floor;
			for (unsigned j = 0; j < mp.AttachedLines.Size(); j++)
			{
				if (mp.AttachedLines[j] == line)
					return true;
			}
		}
	}
	return false;
}

//*****************************************************************************
//	Client state DoScroller and DoPusher carry, and the plane dump rows.

static SnapScrollPushClientStats g_snapSP;

void zx::netsnapshot::SpecialCountScroller( bool bControl, bool bAccel, bool bMoving )
{
	++g_snapSP.scrollers;
	if ( bControl )
		++g_snapSP.controlled;
	if ( bAccel )
	{
		++g_snapSP.accelerative;
		if ( bMoving )
			++g_snapSP.moving;
	}
}

void zx::netsnapshot::SpecialCountPointPusher( bool bSource )
{
	++g_snapSP.points;
	if ( bSource == false )
		++g_snapSP.noSource;
}

void zx::netsnapshot::SpecialClientStats( SnapScrollPushClientStats &st, bool bReset )
{
	st = g_snapSP;
	if ( bReset )
		memset( &g_snapSP, 0, sizeof( g_snapSP ));
}

//*****************************************************************************
//	The join's own command: the state the legacy commands leave out.

// A chunk's record count. 48 scroller records are 720 bytes and 48 pusher
// records 816, so a chunk fits a packet at the 1550-byte default.
#define SNAP_SPECIAL_CHUNK	48

// Which join ordinals are taken, so the search a mismatched echo falls back on
// cannot claim a mover twice; one array per kind, as their ordinals are separate.
static TArray<bool>	g_scrollerClaimed;
static TArray<bool>	g_pusherClaimed;

// The scrollers and pushers in the order SERVER_UpdateMovers sends them, which
// is the order a client creates and iterates them in.
static void special_Scrollers( TArray<DScroller *> &out )
{
	out.Clear( );
	TThinkerIterator<DScroller> it;
	DScroller *p;
	while (( p = it.Next( )) != NULL )
		out.Push( p );
}

static void special_Pushers( TArray<DPusher *> &out )
{
	out.Clear( );
	TThinkerIterator<DPusher> it;
	DPusher *p;
	while (( p = it.Next( )) != NULL )
		out.Push( p );
}

void zx::netsnapshot::SpecialSendJoinState( unsigned int client )
{
	if (( NETWORK_GetState( ) != NETSTATE_SERVER ) || ( zx::netsnapshot::GateOn( ) == false ))
		return;

	TArray<DScroller *> scrollers;
	TArray<DPusher *> pushers;
	special_Scrollers( scrollers );
	special_Pushers( pushers );

	TArray<ServerCommands::SnapScrollerState> recs;
	TArray<ServerCommands::SnapPusherState> none;
	for ( unsigned int i = 0; i < scrollers.Size( ); i += SNAP_SPECIAL_CHUNK )
	{
		recs.Clear( );
		for ( unsigned int j = i; ( j < scrollers.Size( ))
			&& ( j < i + SNAP_SPECIAL_CHUNK ); ++j )
		{
			ServerCommands::SnapScrollerState r;
			r.type = scrollers[j]->ScrollType( );
			r.affectee = scrollers[j]->Affectee( );
			// A scroller without a control sector has no last height to carry.
			r.lastHeight = scrollers[j]->HasControl( )
				? scrollers[j]->LastHeight( ) : fixed_t( 0 );
			r.vdx = scrollers[j]->VelocityX( );
			r.vdy = scrollers[j]->VelocityY( );
			recs.Push( r );
		}
		SERVERCOMMANDS_SnapSpecialState( (int)i, recs, 0, none, client,
			SVCF_ONLYTHISCLIENT );
	}

	TArray<ServerCommands::SnapPusherState> precs;
	TArray<ServerCommands::SnapScrollerState> noneS;
	for ( unsigned int i = 0; i < pushers.Size( ); i += SNAP_SPECIAL_CHUNK )
	{
		precs.Clear( );
		for ( unsigned int j = i; ( j < pushers.Size( ))
			&& ( j < i + SNAP_SPECIAL_CHUNK ); ++j )
		{
			ServerCommands::SnapPusherState r;
			r.type = pushers[j]->PushType( );
			r.affectee = pushers[j]->Affectee( );
			// Only a point pusher has a centre, a radius and a source.
			const bool bPoint = pushers[j]->IsPoint( );
			r.x = bPoint ? pushers[j]->PointX( ) : 0;
			r.y = bPoint ? pushers[j]->PointY( ) : 0;
			r.radius = bPoint ? pushers[j]->PointRadius( ) : 0;
			AActor *pSource = pushers[j]->SourceActor( );
			r.sourceID = ( bPoint && ( pSource != NULL )) ? pSource->NetID : 0;
			precs.Push( r );
		}
		SERVERCOMMANDS_SnapSpecialState( 0, noneS, (int)i, precs, client,
			SVCF_ONLYTHISCLIENT );
	}
}

// The mover a record belongs to: its ordinal, then a search when the echo says
// that ordinal holds something else, as a refused DoScroller shifts them all.
template <typename T>
static T *special_Match( TArray<T *> &all, TArray<bool> &claimed, unsigned int ordinal,
	int type, int affectee, int ( T::*kind )( ) const )
{
	// Push, not Resize: TArray default-initialises, which leaves a bool indeterminate,
	// and a claim read as taken would refuse every record.
	while ( claimed.Size( ) < all.Size( ))
		claimed.Push( false );

	if ( ordinal < all.Size( ))
	{
		T *p = all[ordinal];
		if (( claimed[ordinal] == false )
			&& (( p->*kind )( ) == type ) && ( p->Affectee( ) == affectee ))
		{
			claimed[ordinal] = true;
			return p;
		}
	}

	++g_snapSP.stateEchoMismatch;
	for ( unsigned int i = 0; i < all.Size( ); ++i )
	{
		if ( claimed[i] )
			continue;
		if ((( all[i]->*kind )( ) == type ) && ( all[i]->Affectee( ) == affectee ))
		{
			claimed[i] = true;
			++g_snapSP.stateFallback;
			return all[i];
		}
	}
	++g_snapSP.stateUnmatched;
	return NULL;
}

void zx::netsnapshot::SpecialApplyJoinState( int firstScroller,
	const TArray<struct ServerCommands::SnapScrollerState> &scrollers, int firstPusher,
	const TArray<struct ServerCommands::SnapPusherState> &pushers )
{
	if ( NETWORK_InClientMode( ) == false )
		return;

	++g_snapSP.stateChunks;

	if ( scrollers.Size( ) > 0 )
	{
		TArray<DScroller *> all;
		special_Scrollers( all );
		for ( unsigned int i = 0; i < scrollers.Size( ); ++i )
		{
			const ServerCommands::SnapScrollerState &r = scrollers[i];
			DScroller *p = special_Match<DScroller>( all, g_scrollerClaimed,
				(unsigned int)firstScroller + i, r.type, r.affectee,
				&DScroller::ScrollType );
			if ( p == NULL )
				continue;
			if ( p->HasControl( ))
				p->SetLastHeight( r.lastHeight );
			p->SetVelocity( r.vdx, r.vdy );
			++g_snapSP.stateScrollers;
		}
	}

	if ( pushers.Size( ) > 0 )
	{
		TArray<DPusher *> all;
		special_Pushers( all );
		for ( unsigned int i = 0; i < pushers.Size( ); ++i )
		{
			const ServerCommands::SnapPusherState &r = pushers[i];
			DPusher *p = special_Match<DPusher>( all, g_pusherClaimed,
				(unsigned int)firstPusher + i, r.type, r.affectee,
				&DPusher::PushType );
			if ( p == NULL )
				continue;
			// Wind and current have no centre, and SetPointSource is the point
			// pusher's only writer.
			if ( p->IsPoint( ))
				p->SetPointSource( r.x, r.y, r.radius, (unsigned short)r.sourceID );
			++g_snapSP.statePushers;
		}
	}
}

// A pusher's source by NetID, the real actor first and then its mirror, dropped
// when the id holds a class P_SpawnPushers does not take.
AActor *zx::netsnapshot::SpecialResolveSource( unsigned short &netID, bool &bSeen )
{
	// A server has its own source, and an id of zero is a pusher no join record named.
	if (( NETWORK_InClientMode( ) == false ) || ( netID == 0 ))
		return NULL;

	bool bMirror = false;
	AActor *pActor = CLIENT_FindThingByNetID( netID );
	if ( pActor == NULL )
	{
		pActor = MirrorForNetID( netID, false );
		bMirror = ( pActor != NULL );
	}

	if ( pActor == NULL )
	{
		if ( CLIENT_PREDICT_IsPredicting( ) == false )
			++g_snapSP.unresolved;
		return NULL;
	}

	const FName type = pActor->GetClass( )->TypeName;
	if (( type != NAME_PointPusher ) && ( type != NAME_PointPuller ))
	{
		netID = 0;
		++g_snapSP.gone;
		return NULL;
	}

	if ( bSeen )
		++g_snapSP.reresolved;
	else
	{
		bSeen = true;
		++g_snapSP.resolved;
		if ( bMirror )
			++g_snapSP.resolvedMirror;
	}
	return pActor;
}

// Destroys every scroller, pusher and waggle ahead of a repeated full update,
// which re-sends them (a CLIENTSIDE script's scroller going for good).
void zx::netsnapshot::SpecialClearForFullUpdate( )
{
	g_scrollerClaimed.Clear( );
	g_pusherClaimed.Clear( );
	TThinkerIterator<DScroller> scrollers;
	DScroller *pScroller;
	while (( pScroller = scrollers.Next( )) != NULL )
	{
		pScroller->Destroy( );
		++g_snapSP.replacedScrollers;
	}

	TThinkerIterator<DPusher> pushers;
	DPusher *pPusher;
	while (( pPusher = pushers.Next( )) != NULL )
	{
		pPusher->Destroy( );
		++g_snapSP.replacedPushers;
	}

	// The re-sent waggle sets its plane from m_OriginalDist on its first tic.
	TThinkerIterator<DWaggleBase> waggles;
	DWaggleBase *pWaggle;
	while (( pWaggle = waggles.Next( )) != NULL )
	{
		pWaggle->Destroy( );
		++g_snapSP.replacedWaggles;
	}
}

void DScroller::SnapDump () const
{
	static const char *const names[] = { "side", "floor", "ceil", "carry", "carryc" };
	// m_LastHeight is set only with a control sector.
	Printf( "  scroll %s %d parts %d rate %d %d ctl %d last %d vel %d %d\n",
		( (unsigned)m_Type < countof( names )) ? names[m_Type] : "?", m_Affectee,
		m_Parts, (int)m_dx, (int)m_dy, m_Control,
		( m_Control != -1 ) ? (int)m_LastHeight : 0,
		(int)m_vdx, (int)m_vdy );
}

void DPusher::SnapDump () const
{
	static const char *const names[] = { "push", "pull", "wind", "current" };
	// The server names its source, a client what it resolved.
	AActor *pSource = const_cast<TObjPtr<AActor> &>( m_Source );
	const char *pszState = ( pSource == NULL ) ? "none"
		: ( pSource->NetID != 0 ) ? "real" : "mirror";
	const unsigned int netID = NETWORK_InClientMode( ) ? m_SourceNetID
		: (( pSource != NULL ) ? pSource->NetID : 0 );
	// Unresolved prints no id: a client keeps the id of a source it saw
	// removed, where the server holds none.
	char source[24];
	if ( pSource == NULL )
		mysnprintf( source, sizeof( source ), "none" );
	else
		mysnprintf( source, sizeof( source ), "%u %s", netID, pszState );
	Printf( "  push %s %d mag %d radius %d at %d %d source %s\n",
		( (unsigned)m_Type < countof( names )) ? names[m_Type] : "?", m_Affectee,
		m_Magnitude, ( m_Type == p_push ) ? m_Radius : 0,
		( m_Type == p_push ) ? m_X : 0, ( m_Type == p_push ) ? m_Y : 0,
		source );
}

//*****************************************************************************
//	DPlat::Tick's client counts.

bool zx::netsnapshot::MoverHoldsPlane( bool bBlocked )
{
	if (( bBlocked == false ) || ( NETWORK_InClientMode( ) == false ))
		return false;
	MoverCountHeld( );
	return true;
}

void zx::netsnapshot::MoverHeldPlanes( BYTE blocked, bool & bFloor, bool & bCeiling )
{
	const bool bClient = NETWORK_InClientMode( );
	bFloor = bClient && (( blocked & SNAPMV_PLANE_FLOOR ) != 0 );
	bCeiling = bClient && (( blocked & SNAPMV_PLANE_CEILING ) != 0 );
	if ( bFloor || bCeiling )
	{
		MoverCountHeld( );
		if ( bFloor != bCeiling )
			MoverCountHeldOnePlane( );
	}
}

bool zx::netsnapshot::MoverPlaneStalled( bool bWas, bool bStalled )
{
	// A client is told whether the plane stalled; only a server works it out.
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return bWas;
	return bStalled;
}

BYTE zx::netsnapshot::MoverPlanesStalled( BYTE was, bool bFloor, bool bCeiling )
{
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return was;
	return (BYTE)(( bFloor ? SNAPMV_PLANE_FLOOR : 0 )
		| ( bCeiling ? SNAPMV_PLANE_CEILING : 0 ));
}

void zx::netsnapshot::MoverCountCrushed()
{
	// A crush the hold did not prevent, which only a client can be wrong about.
	if ( NETWORK_InClientMode( ) == false )
		return;
	++g_moverCrushed;
}

void zx::netsnapshot::MoverCountPlatWaitTick()
{
	if ( NETWORK_InClientMode( ) == false )
		return;
	++g_moverWaitTicks;
}

void zx::netsnapshot::MoverCountPlatWaitExpired()
{
	if ( NETWORK_InClientMode( ) == false )
		return;
	++g_moverWaitExpired;
}

//============================================================================
//
// LinkCensus
//
// Reports the link graph the join pass orders (LinkWriteOrder), and any
// cycle, which no order fixes.
//
//============================================================================

static void snap_BuildLinkEdges(TArray<SnapLinkEdge> &out)
{
	out.Clear();
	for (int i = 0; i < numsectors; i++)
	{
		if (sectors[i].e == NULL)
			continue;
		for (int p = 0; p < 2; p++)
		{
			extsector_t::linked::plane &sp = p ? sectors[i].e->Linked.Ceiling
				: sectors[i].e->Linked.Floor;
			for (unsigned j = 0; j < sp.Sectors.Size(); j++)
			{
				SnapLinkEdge e;
				e.from = i;
				e.to = (int)(sp.Sectors[j].Sector - sectors);
				e.type = sp.Sectors[j].Type;
				out.Push(e);
			}
		}
	}
}

// The join's plane order over this map's link graph (ComputeLinkWriteOrder).
int zx::netsnapshot::LinkWriteOrder(int *order, BYTE *drivenPlanes)
{
	TArray<SnapLinkEdge> edges;
	snap_BuildLinkEdges(edges);
	return ComputeLinkWriteOrder(numsectors, (edges.Size() > 0) ? &edges[0] : NULL, edges.Size(),
		P_LinkPlaneBits(false), P_LinkPlaneBits(true), order, drivenPlanes);
}

// Whether a direct control (propagation is one level deep) of this plane is
// owned, ownership tested first.
bool zx::netsnapshot::LinkControlOwned(sector_t *sector, bool ceiling)
{
	const int bits = P_LinkPlaneBits(ceiling);

	for (int i = 0; i < numsectors; i++)
	{
		for (int p = 0; p < 2; p++)
		{
			if (MoverOwnsPlane(&sectors[i], p != 0) == false)
				continue;
			extsector_t::linked::plane &sp = p ? sectors[i].e->Linked.Ceiling
				: sectors[i].e->Linked.Floor;
			for (unsigned j = 0; j < sp.Sectors.Size(); j++)
			{
				if ((sp.Sectors[j].Sector == sector) && (sp.Sectors[j].Type & bits))
					return true;
			}
		}
	}
	return false;
}

void zx::netsnapshot::LinkCensus()
{
	// [Kaori-Chani] The setting is read here rather than at the hook, which is one call.
	if ((fua_snap_linkcensus == false) || (numsectors <= 0))
		return;

	TArray<SnapLinkEdge> edges;
	snap_BuildLinkEdges(edges);

	if (edges.Size() == 0)
	{
		Printf("[link] %d sectors, no links\n", numsectors);
		return;
	}

	TArray<int> indeg, depth;
	TArray<bool> involved, driven;
	indeg.Resize(numsectors);
	depth.Resize(numsectors);
	involved.Resize(numsectors);
	driven.Resize(numsectors);
	for (int i = 0; i < numsectors; i++)
	{
		indeg[i] = 0;
		depth[i] = 0;
		involved[i] = false;
		driven[i] = false;
	}
	for (unsigned k = 0; k < edges.Size(); k++)
	{
		indeg[edges[k].to]++;
		driven[edges[k].to] = true;
		involved[edges[k].from] = true;
		involved[edges[k].to] = true;
	}

	int nodes = 0;
	TArray<int> order;
	for (int i = 0; i < numsectors; i++)
	{
		if (involved[i] == false)
			continue;
		nodes++;
		if (indeg[i] == 0)
			order.Push(i);
	}

	// Kahn, relaxing depth on every edge rather than only on the one that frees
	// a node, so depth is the longest path and not the first path found.
	unsigned head = 0;
	while (head < order.Size())
	{
		const int n = order[head++];
		for (unsigned k = 0; k < edges.Size(); k++)
		{
			if (edges[k].from != n)
				continue;
			const int m = edges[k].to;
			if (depth[m] < depth[n] + 1)
				depth[m] = depth[n] + 1;
			if (--indeg[m] == 0)
				order.Push(m);
		}
	}
	const bool bCycle = ((int)order.Size() < nodes);

	int maxDepth = 0;
	for (int i = 0; i < numsectors; i++)
	{
		if (involved[i] && (depth[i] > maxDepth))
			maxDepth = depth[i];
	}

	int chain = 0;
	for (int i = 0; i < numsectors; i++)
	{
		if ((driven[i] == false) || (sectors[i].e == NULL))
			continue;
		if (sectors[i].e->Linked.Floor.Sectors.Size()
			+ sectors[i].e->Linked.Ceiling.Sectors.Size() > 0)
			chain++;
	}

	// Inverted edges are the ones an index-ordered pass would get wrong, and
	// those with both ends flagged the ones it would show at this join.
	int inverted = 0, doubledFloor = 0, doubledCeil = 0, floorEdges = 0, ceilEdges = 0;
	for (unsigned k = 0; k < edges.Size(); k++)
	{
		const SnapLinkEdge &e = edges[k];
		const bool bFloor = !!(e.type & P_LinkPlaneBits(false));
		const bool bCeil = !!(e.type & P_LinkPlaneBits(true));
		if (bFloor)
			floorEdges++;
		if (bCeil)
			ceilEdges++;
		if (e.to >= e.from)
			continue;
		inverted++;
		if (bFloor && sectors[e.from].bFloorHeightChange
			&& sectors[e.to].bFloorHeightChange)
			doubledFloor++;
		if (bCeil && sectors[e.from].bCeilingHeightChange
			&& sectors[e.to].bCeilingHeightChange)
			doubledCeil++;
	}

	Printf("[link] %d sectors, %u edges (%d move floor, %d move ceiling)"
		" | %d nodes, %d also control (chained) | depth %d%s\n",
		numsectors, edges.Size(), floorEdges, ceilEdges, nodes, chain,
		maxDepth, bCycle ? " CYCLE" : "");
	Printf("[link] %d edges inverted (driven index < control), %d floor and"
		" %d ceiling with both ends flagged | join order: %s\n", inverted,
		doubledFloor, doubledCeil, bCycle ? "CYCLE, not orderable" : "complete");

	unsigned shown = 0;
	for (unsigned k = 0; k < edges.Size(); k++)
	{
		const SnapLinkEdge &e = edges[k];
		if (e.to >= e.from)
			continue;
		if (shown++ >= 32)
			continue;
		Printf("[link]   sec %d <- control sec %d type %d | flagged driven %d/%d"
			" control %d/%d\n", e.to, e.from, e.type,
			(int)sectors[e.to].bFloorHeightChange,
			(int)sectors[e.to].bCeilingHeightChange,
			(int)sectors[e.from].bFloorHeightChange,
			(int)sectors[e.from].bCeilingHeightChange);
	}
	if (shown > 32)
		Printf("[link]   ... %u more inverted edges\n", shown - 32);
}
