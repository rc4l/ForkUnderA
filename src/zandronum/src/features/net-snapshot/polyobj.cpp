//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: polyobj.cpp
//
// Description: The snapshot path's polyobjects: the action list, the
// server's capture, the actions' capture and apply, and the client's record
// sink. See polyobj.h.
//
//-----------------------------------------------------------------------------

#include <string.h>

#include "doomdef.h"
#include "p_local.h"
#include "s_sndseq.h"
#include "r_data/r_interpolate.h"
#include "g_level.h"
#include "po_man.h"
#include "network.h"
#include "sv_commands.h"
#include "features/net-snapshot/format.h"
#include "features/net-snapshot/computation/mover_compute.h"
#include "features/net-snapshot/capture.h"
#include "features/net-snapshot/entities.h"
#include "features/net-snapshot/movers.h"
#include "features/net-snapshot/polyobj.h"

using namespace zx::netsnapshot;

// po_man.cpp declares no header for it; g_game.cpp and sv_main.cpp do the same.
FPolyObj *GetPolyobjByIndex( ULONG ulPolyIdx );

// Onto the end of the polyobject's action list, once.
BYTE zx::netsnapshot::PolyNewGen()
{
	if ( NETWORK_GetState( ) != NETSTATE_SERVER )
		return 0;
	return NewMoverGen( );
}

void zx::netsnapshot::PolySendStartSound( int polyNum )
{
	// Gate off, the client's own handler starts it, so stock's bytes stand.
	if ( MoverGate( ) == false )
		return;
	SERVERCOMMANDS_PlayPolyobjSound( polyNum, 0 );
}

void zx::netsnapshot::PolySendStopSound( int polyNum )
{
	// Gate off, stock's silence, so its bytes stand and its lingering sequence stays stock's.
	if ( MoverGate( ) == false )
		return;
	SERVERCOMMANDS_StopPolyobjSound( polyNum );
}

void zx::netsnapshot::PolyLink( FPolyObj *poly, DPolyAction *pAction )
{
	DPolyAction **pp = &poly->snapActions;
	while ( *pp != NULL )
	{
		if ( *pp == pAction )
			return;
		pp = &( *pp )->m_SnapNext;
	}
	pAction->m_SnapNext = NULL;
	*pp = pAction;
}

void zx::netsnapshot::PolyUnlink( FPolyObj *poly, DPolyAction *pAction )
{
	for ( DPolyAction **pp = &poly->snapActions; *pp != NULL; pp = &( *pp )->m_SnapNext )
	{
		if ( *pp == pAction )
		{
			*pp = pAction->m_SnapNext;
			pAction->m_SnapNext = NULL;
			return;
		}
	}
}

// Captures the whole polyobject as one record (7.1), entries past the body's
// room left out and counted; `bAllocate` false never takes a slot.
void zx::netsnapshot::PolyCapture( FPolyObj *poly, bool bAllocate )
{
	if (( poly == NULL ) || ( MoversOn( ) == false ))
		return;

	SnapMoverPoly s;
	memset( &s, 0, sizeof( s ));	// padding; the dirty compare reads it
	s.x = poly->StartSpot.x;
	s.y = poly->StartSpot.y;
	s.angle = poly->angle;
	// Budgeted in the widest frame, so the body fits whatever the map's; capture
	// runs at teardown too, where the map's frame must not be taken.
	const SnapFrame widest = FrameWidest( );
	unsigned int bytes = PolyHeaderBytes( widest ), over = 0;
	for ( DPolyAction *a = poly->snapActions; a != NULL; a = a->m_SnapNext )
	{
		const BYTE type = a->SnapType( );
		if ( type == 0xFF )
			continue;
		const unsigned int b = PolyEntryBytes( widest, type );
		if (( s.count >= SNAPPO_MAXENTRIES ) || ( bytes + b > SNAPMV_BODY_MAX ))
		{
			++over;
			continue;
		}
		SnapPolyEntry &e = s.e[s.count++];
		a->SnapFill( e );
		if ( poly->specialdata == a )
			e.kind |= SNAPPO_SPECIAL;
		bytes += b;
	}
	MoverCapturePoly( poly, (WORD)( poly - polyobjs ), s, over, bAllocate );
}

void DPolyAction::SnapCapture ()
{
	PolyCapture( PO_GetPolyobj( m_PolyObj ));
}

BYTE DPolyAction::SnapType () const
{
	return 0xFF;
}

void DPolyAction::SnapFill ( SnapPolyEntry &e ) const
{
	// TObjPtr compares only non-const.
	const bool bNoInterp =
		( const_cast<TObjPtr<DInterpolation> &>( m_Interpolation ) == NULL );
	e.kind = (BYTE)( SnapType( ) | ( m_SnapBlocked ? SNAPPO_BLOCKED : 0 )
		| ( bNoInterp ? SNAPPO_NOINTERP : 0 ));
	e.gen = m_SnapGen;
	e.speed = m_Speed;
	e.dist = m_Dist;
}

void DPolyAction::SnapLoad ( const SnapPolyEntry &e )
{
	m_Speed = e.speed;
	m_Dist = e.dist;
	m_SnapGen = e.gen;
	m_SnapBlocked = (( e.kind & SNAPPO_BLOCKED ) != 0 );
	m_SnapSeen = true;
}

bool DPolyAction::SnapDone () const
{
	return false;
}

BYTE DRotatePoly::SnapType () const
{
	return SNAPPO_ROTATE;
}

// Done at 0, which a full turn reaches only through the last-step clamp (-1
// is perpetual).
bool DRotatePoly::SnapDone () const
{
	return m_Dist == 0;
}

BYTE DMovePoly::SnapType () const
{
	return SNAPPO_MOVE;
}

void DMovePoly::SnapFill ( SnapPolyEntry &e ) const
{
	Super::SnapFill( e );
	e.angle = (SWORD)m_Angle;
	e.xSpeed = m_xSpeed;
	e.ySpeed = m_ySpeed;
}

void DMovePoly::SnapLoad ( const SnapPolyEntry &e )
{
	Super::SnapLoad( e );
	m_Angle = e.angle;
	m_xSpeed = e.xSpeed;
	m_ySpeed = e.ySpeed;
}

bool DMovePoly::SnapDone () const
{
	return m_Dist <= 0;
}

BYTE DMovePolyTo::SnapType () const
{
	return SNAPPO_MOVETO;
}

void DMovePolyTo::SnapFill ( SnapPolyEntry &e ) const
{
	Super::SnapFill( e );
	e.xSpeed = m_xSpeed;
	e.ySpeed = m_ySpeed;
	e.xTarget = m_xTarget;
	e.yTarget = m_yTarget;
}

void DMovePolyTo::SnapLoad ( const SnapPolyEntry &e )
{
	Super::SnapLoad( e );
	m_xSpeed = e.xSpeed;
	m_ySpeed = e.ySpeed;
	m_xTarget = e.xTarget;
	m_yTarget = e.yTarget;
}

bool DMovePolyTo::SnapDone () const
{
	return m_Dist <= 0;
}

BYTE DPolyDoor::SnapType () const
{
	if ( m_Type == PODOOR_SLIDE )
		return SNAPPO_SLIDE;
	if ( m_Type == PODOOR_SWING )
		return SNAPPO_SWING;
	return 0xFF;
}

// DMovePoly's m_Angle is skipped: a door never reads it.
void DPolyDoor::SnapFill ( SnapPolyEntry &e ) const
{
	DPolyAction::SnapFill( e );
	e.xSpeed = m_xSpeed;
	e.ySpeed = m_ySpeed;
	e.direction = (SWORD)m_Direction;
	e.totalDist = m_TotalDist;
	e.tics = m_Tics;
	e.waitTics = m_WaitTics;
	e.close = m_Close ? 1 : 0;
}

void DPolyDoor::SnapLoad ( const SnapPolyEntry &e )
{
	DPolyAction::SnapLoad( e );
	m_xSpeed = e.xSpeed;
	m_ySpeed = e.ySpeed;
	m_Direction = e.direction;
	m_TotalDist = e.totalDist;
	m_Tics = e.tics;
	m_WaitTics = e.waitTics;
	m_Close = ( e.close != 0 );
}

// Neither type clamps its last step, and a swing's -1 is perpetual.
bool DPolyDoor::SnapDone () const
{
	if ( m_Tics != 0 )
		return false;
	if (( m_Type == PODOOR_SWING ) && ( m_Dist == -1 ))
		return false;
	return m_Dist <= 0;
}

// ===== The client's polyobject sink (7.1) =====

// Per report.
static unsigned int g_polyRecords = 0;
static unsigned int g_polyCreated = 0;
static unsigned int g_polyEvicted = 0;
static unsigned int g_polyGone = 0;
static unsigned int g_polyGoneNoLocal = 0;
static unsigned int g_polyStale = 0;
static unsigned int g_polyUnusable = 0;
static unsigned int g_polyUngated = 0;
static unsigned int g_polyHeld = 0;
static unsigned int g_polyDone = 0;
static unsigned int g_polySet[SNAPPO_SET_REASONS] = { 0 };
static unsigned int g_polySetWorstMove = 0;	// fixed units
static unsigned int g_polySetWorstTurn = 0;	// angle units
// Records re-anchoring a perpetual rotator and their largest correction, after
// the first the change in latency.
static unsigned int g_polyResyncs = 0;
static unsigned int g_polyResyncWorst = 0;	// angle units
static unsigned int g_polySuppressed = 0;

// Held while blocked, or done: the countdown ran out before the record that
// ends it, which is how an action ordinarily ends on a client.
bool DPolyAction::SnapHolds ()
{
	if ( m_SnapBlocked )
	{
		++g_polyHeld;
		return true;
	}
	if ( SnapDone( ))
	{
		++g_polyDone;
		return true;
	}
	return false;
}

// Last frame applied per polyobject, by index; rebuilt as the sector
// stamps are, when cl_entities' stream epoch moves.
static int * g_polyStamp = NULL;
static int g_polyStampCount = 0;
static unsigned int g_polyStampEpoch = 0;

static int & snappo_Stamp( WORD index )
{
	const unsigned int epoch = StreamEpoch( );
	if (( g_polyStampCount != po_NumPolyobjs ) || ( g_polyStampEpoch != epoch ))
	{
		delete[] g_polyStamp;
		g_polyStamp = new int[po_NumPolyobjs > 0 ? po_NumPolyobjs : 1];
		memset( g_polyStamp, 0, ( po_NumPolyobjs > 0 ? po_NumPolyobjs : 1 ) * sizeof( int ));
		g_polyStampCount = po_NumPolyobjs;
		g_polyStampEpoch = epoch;
	}
	return g_polyStamp[index];
}

// The newest record ignored as legacy-live, per polyobject, kept for a gate-on
// since the server will not re-send it.
struct SnapPolyKept
{
	bool	bHave;
	BYTE	flags;
	BYTE	bodyBytes;
	int		frame;
	BYTE	body[SNAPMV_BODY_MAX];
};
static TArray<SnapPolyKept> g_polyKept;
static unsigned int g_polyKeptEpoch = 0;

static void snappo_Keep( BYTE flags, WORD index, const BYTE *pBody,
	unsigned int bodyBytes, int frame )
{
	const unsigned int epoch = StreamEpoch( );
	if (( g_polyKept.Size( ) != (unsigned int)po_NumPolyobjs ) || ( g_polyKeptEpoch != epoch ))
	{
		g_polyKept.Resize( po_NumPolyobjs > 0 ? po_NumPolyobjs : 0 );
		for ( unsigned int i = 0; i < g_polyKept.Size( ); ++i )
			g_polyKept[i].bHave = false;
		g_polyKeptEpoch = epoch;
	}
	if (( index >= g_polyKept.Size( )) || ( bodyBytes > SNAPMV_BODY_MAX ))
		return;
	SnapPolyKept &k = g_polyKept[index];
	if ( k.bHave && (( frame - k.frame ) < 0 ))
		return;
	k.bHave = true;
	k.flags = flags;
	k.bodyBytes = (BYTE)bodyBytes;
	k.frame = frame;
	memcpy( k.body, pBody, bodyBytes );
}

static DPolyAction * snappo_Create( FPolyObj *poly, BYTE type )
{
	switch ( type )
	{
	case SNAPPO_ROTATE:	return new DRotatePoly( poly->tag );
	case SNAPPO_MOVE:	return new DMovePoly( poly->tag );
	case SNAPPO_MOVETO:	return new DMovePolyTo( poly->tag );
	case SNAPPO_SLIDE:	return new DPolyDoor( poly->tag, PODOOR_SLIDE );
	case SNAPPO_SWING:	return new DPolyDoor( poly->tag, PODOOR_SWING );
	default:			return NULL;
	}
}

// Writes the axes the record says to take, StartSpot and angle, both absolute.
static void snappo_WriteAxes( FPolyObj *poly, const SnapMoverPoly &s,
	bool bMove, int moveReason, bool bTurn, int turnReason )
{
	if ( bMove )
	{
		const fixed_t dx = s.x - poly->StartSpot.x;
		const fixed_t dy = s.y - poly->StartSpot.y;
		if (( dx != 0 ) || ( dy != 0 ))
		{
			// [Kaori-Chani] MovePolyobj takes ints that are themselves 16.16 values, in both
			// trees, so the raw of each offset is what it has always been given.
			poly->MovePolyobj( (int)dx, (int)dy, true );
			++g_polySet[moveReason];
			const unsigned int off = (unsigned int)MAX( abs( dx ), abs( dy ));
			if ( off > g_polySetWorstMove )
				g_polySetWorstMove = off;
		}
	}
	if ( bTurn )
	{
		const angle_t da = s.angle - poly->angle;
		if ( da != 0 )
		{
			poly->RotatePolyobj( da );
			++g_polySet[turnReason];
			const unsigned int off = (unsigned int)abs( (int)da );
			if ( off > g_polySetWorstTurn )
				g_polySetWorstTurn = off;
			if (( turnReason == SNAPPO_SET_RESYNC ) && ( off > g_polyResyncWorst ))
				g_polyResyncWorst = off;
		}
	}
}

// Destroys the record-made actions not in `keep`, leaving client-made ones.
static unsigned int snappo_DestroyUnlisted( FPolyObj *poly, DPolyAction * const *keep,
	unsigned int nKeep )
{
	DPolyAction *dead[SNAPPO_MAXENTRIES * 2];
	unsigned int nDead = 0;
	for ( DPolyAction *a = poly->snapActions; a != NULL; a = a->m_SnapNext )
	{
		if ( a->m_SnapSeen == false )
			continue;
		bool bKeep = false;
		for ( unsigned int i = 0; i < nKeep; ++i )
		{
			if ( keep[i] == a )
				bKeep = true;
		}
		if (( bKeep == false ) && ( nDead < SNAPPO_MAXENTRIES * 2 ))
			dead[nDead++] = a;
	}
	for ( unsigned int i = 0; i < nDead; ++i )
		dead[i]->Destroy( );
	return nDead;
}

// Applies one polyobject record, from the mover sink or the hold's replay,
// reconciling the client's actions to it by generation and type.
void zx::netsnapshot::PolyApplyRecord( BYTE flags, WORD index, const BYTE *pBody,
	unsigned int bodyBytes, int frame )
{
	++g_polyRecords;
	FPolyObj *poly = GetPolyobjByIndex( index );
	SnapMoverPoly s;
	const bool bGone = (( flags & SNAPMVF_GONE ) != 0 );
	if (( poly == NULL )
		|| ( ReadMoverPoly( pBody, ClientMapFrame( ), bodyBytes, s ) == false )
		|| (( s.count == 0 ) != bGone ))
	{
		++g_polyUnusable;
		MoverCountOutcome( SNAPMV_OUT_UNUSABLE );
		return;
	}

	// Beside live legacy handlers the record would make a second set of actions
	// driving the same polyobject; legacy keeps it.
	if ( MoverSuppressesLegacy( SNAPMV_POLY ) == false )
	{
		++g_polyUngated;
		MoverCountOutcome( SNAPMV_OUT_UNUSABLE );
		snappo_Keep( flags, index, pBody, bodyBytes, frame );
		return;
	}

	// Ordering guard, per polyobject: the record is its whole state.
	int &stamp = snappo_Stamp( index );
	if ( frame < stamp )
	{
		++g_polyStale;
		MoverCountOutcome( SNAPMV_OUT_STALE );
		return;
	}
	stamp = frame;

	bool bHad = false;
	for ( DPolyAction *a = poly->snapActions; a != NULL; a = a->m_SnapNext )
	{
		if ( a->m_SnapSeen )
			bHad = true;
	}

	if ( bGone )
	{
		++g_polyGone;
		MoverCountOutcome( SNAPMV_OUT_GONE );
		if ( bHad == false )
			++g_polyGoneNoLocal;
		snappo_DestroyUnlisted( poly, NULL, 0 );
		snappo_WriteAxes( poly, s, true, SNAPPO_SET_GONE, true, SNAPPO_SET_GONE );
		return;
	}

	DPolyAction *listed[SNAPPO_MAXENTRIES];
	bool bMoveNew = false, bTurnNew = false;	// an axis gained an action
	for ( unsigned int i = 0; i < s.count; ++i )
	{
		const SnapPolyEntry &e = s.e[i];
		const BYTE type = (BYTE)( e.kind & SNAPPO_TYPEMASK );
		DPolyAction *pAction = NULL;
		for ( DPolyAction *a = poly->snapActions; a != NULL; a = a->m_SnapNext )
		{
			if ( a->m_SnapSeen && ( a->m_SnapGen == e.gen ) && ( a->SnapType( ) == type ))
			{
				pAction = a;
				break;
			}
		}
		if ( pAction == NULL )
		{
			pAction = snappo_Create( poly, type );
			if ( pAction == NULL )
			{
				listed[i] = NULL;	// the body walk refuses an unknown type
				continue;
			}
			++g_polyCreated;
			( ComputePolyTurns( type ) ? bTurnNew : bMoveNew ) = true;
			// The server drops it for a move of 2 tics or fewer.
			if ( e.kind & SNAPPO_NOINTERP )
				pAction->StopInterpolation( );
		}
		pAction->SnapLoad( e );
		if ( pAction->m_SnapBlocked )
			MoverCountBlockedRecv( );
		listed[i] = pAction;
	}
	g_polyEvicted += snappo_DestroyUnlisted( poly, listed, s.count );

	// specialdata as the server's: the marked entry, or none.
	DPolyAction *pSpecial = NULL;
	for ( unsigned int i = 0; i < s.count; ++i )
	{
		if ( s.e[i].kind & SNAPPO_SPECIAL )
			pSpecial = listed[i];
	}
	if (( pSpecial != NULL ) || ( poly->specialdata == NULL )
		|| poly->specialdata->m_SnapSeen )
		poly->specialdata = pSpecial;

	// Per axis on the first record, one giving the axis a new action, or
	// where nothing on it moves, never under a moving action (snap_polyobj.md).
	bool bMoveMoving = false, bMoveBlocked = false;
	bool bTurnMoving = false, bTurnBlocked = false;
	for ( unsigned int i = 0; i < s.count; ++i )
	{
		if ( listed[i] == NULL )
			continue;
		const bool bTurns = ComputePolyTurns( listed[i]->SnapType( ));
		if ( listed[i]->SnapMoving( ))
			( bTurns ? bTurnMoving : bMoveMoving ) = true;
		if ( listed[i]->m_SnapBlocked )
			( bTurns ? bTurnBlocked : bMoveBlocked ) = true;
	}
	const bool bMoveFirst = ( bHad == false ) || bMoveNew;
	const bool bTurnFirst = ( bHad == false ) || bTurnNew;

	// A perpetual rotator's periodic record writes its rotation though it
	// turns, the rotation alone.
	bool bResync = false;
	for ( unsigned int i = 0; ( i < s.count ) && bTurnMoving; ++i )
	{
		if ((( s.e[i].kind & SNAPPO_TYPEMASK ) == SNAPPO_ROTATE ) && ( s.e[i].dist == -1 ))
			bResync = true;
	}
	if ( bResync && ( bTurnFirst == false ))
		++g_polyResyncs;

	snappo_WriteAxes( poly, s,
		bMoveFirst || ( bMoveMoving == false ),
		bMoveFirst ? SNAPPO_SET_FIRST
			: ( bMoveBlocked ? SNAPPO_SET_BLOCKED : SNAPPO_SET_REST ),
		bTurnFirst || ( bTurnMoving == false ) || bResync,
		bTurnFirst ? SNAPPO_SET_FIRST
			: ( bResync ? SNAPPO_SET_RESYNC
			: ( bTurnBlocked ? SNAPPO_SET_BLOCKED : SNAPPO_SET_REST )));
	MoverCountOutcome( SNAPMV_OUT_APPLIED );
}

// Drops a polyobject's legacy-made or record-made actions, and stops its
// sequence if any went.
static unsigned int snappo_GateDrop( bool bLegacyMade )
{
	unsigned int dropped = 0;
	for ( int i = 0; i < po_NumPolyobjs; ++i )
	{
		FPolyObj *poly = &polyobjs[i];
		bool bAny = false;
		for ( DPolyAction *a = poly->snapActions; a != NULL; )
		{
			DPolyAction *next = a->m_SnapNext;
			if ( bLegacyMade ? a->m_SnapLegacy : a->m_SnapSeen )
			{
				a->Destroy( );
				bAny = true;
				++dropped;
			}
			a = next;
		}
		if ( bAny )
			SN_StopSequence( poly );
	}
	return dropped;
}

unsigned int zx::netsnapshot::PolyGateOn( unsigned int &applied )
{
	const unsigned int dropped = snappo_GateDrop( true );
	if ( g_polyKeptEpoch == StreamEpoch( ))
	{
		for ( unsigned int i = 0; i < g_polyKept.Size( ); ++i )
		{
			SnapPolyKept &k = g_polyKept[i];
			if ( k.bHave && (( k.frame - snappo_Stamp( (WORD)i )) >= 0 ))
			{
				PolyApplyRecord( k.flags, (WORD)i, k.body, k.bodyBytes, k.frame );
				++applied;
			}
			k.bHave = false;
		}
	}
	g_polyKept.Clear( );
	return dropped;
}

unsigned int zx::netsnapshot::PolyGateOff( )
{
	g_polyKept.Clear( );
	return snappo_GateDrop( false );
}

// A map reset's: every action on every polyobject, and its sequence.
void zx::netsnapshot::PolyDestroyAll()
{
	// The constructor links every action, legacy-made ones included, so with the gate off
	// this would destroy actions stock's own reset leaves alone.
	if ( MoverSuppressesLegacy( SNAPMV_POLY ) == false )
		return;

	for ( int i = 0; i < po_NumPolyobjs; ++i )
	{
		FPolyObj *poly = &polyobjs[i];
		if ( poly->snapActions == NULL )
			continue;
		SN_StopSequence( poly );
		DPolyAction *a = poly->snapActions;
		while ( a != NULL )
		{
			DPolyAction *pNext = a->m_SnapNext;
			a->Destroy( );
			a = pNext;
		}
		poly->snapActions = NULL;
		poly->specialdata = NULL;
	}
}

// Whether the legacy position and rotation writes must skip this
// polyobject: a record made its actions, outside a reset window.
bool zx::netsnapshot::OwnsPoly( FPolyObj *poly )
{
	if (( poly == NULL ) || MoverInReset( )
		|| ( MoverSuppressesLegacy( SNAPMV_POLY ) == false ))
		return false;
	for ( DPolyAction *a = poly->snapActions; a != NULL; a = a->m_SnapNext )
	{
		if ( a->m_SnapSeen )
			return true;
	}
	return false;
}

// Advances a replayed record's actions by the hold, listing them first since
// an action ending in its Tick unlinks itself.
unsigned int zx::netsnapshot::PolyAdvance( WORD index, int tics )
{
	FPolyObj *poly = GetPolyobjByIndex( index );
	if ( poly == NULL )
		return 0;
	TArray<DPolyAction *> actions;
	for ( DPolyAction *a = poly->snapActions; a != NULL; a = a->m_SnapNext )
	{
		if ( a->m_SnapSeen && ( a->m_SnapBlocked == false ))
			actions.Push( a );
	}
	for ( unsigned int i = 0; i < actions.Size( ); ++i )
	{
		for ( int n = 0; ( n < tics ) && (( actions[i]->ObjectFlags & OF_EuthanizeMe ) == 0 ); ++n )
			actions[i]->Tick( );
	}
	return actions.Size( );
}

void zx::netsnapshot::PolyCountSuppressed()
{
	++g_polySuppressed;
}

void zx::netsnapshot::PolyClientStats( SnapPolyClientStats &st, bool bReset )
{
	st.records = g_polyRecords;
	st.created = g_polyCreated;
	st.evicted = g_polyEvicted;
	st.gone = g_polyGone;
	st.goneNoLocal = g_polyGoneNoLocal;
	st.stale = g_polyStale;
	st.unusable = g_polyUnusable;
	st.ungated = g_polyUngated;
	st.held = g_polyHeld;
	st.done = g_polyDone;
	for ( int i = 0; i < SNAPPO_SET_REASONS; ++i )
		st.set[i] = g_polySet[i];
	st.setWorstMove = g_polySetWorstMove;
	st.setWorstTurn = g_polySetWorstTurn;
	st.resyncs = g_polyResyncs;
	st.resyncWorst = g_polyResyncWorst;
	st.suppressed = g_polySuppressed;
	if ( bReset )
	{
		g_polyRecords = g_polyCreated = g_polyEvicted = g_polyGone = 0;
		g_polyGoneNoLocal = g_polyStale = g_polyUnusable = g_polyUngated = 0;
		g_polyHeld = g_polyDone = g_polySuppressed = 0;
		g_polySetWorstMove = g_polySetWorstTurn = 0;
		g_polyResyncs = g_polyResyncWorst = 0;
		memset( g_polySet, 0, sizeof( g_polySet ));
	}
}
