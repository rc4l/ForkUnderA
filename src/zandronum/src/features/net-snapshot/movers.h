//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: movers.h
//
// Description: The snapshot path's sector movers: the client's record
// sink and its counts, plane ownership, the join's link order, and the
// scroller and pusher state. Included by p_spec.h.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_MOVERS_H
#define ZX_NETSNAPSHOT_MOVERS_H

#include "basictypes.h"

struct sector_t;
struct line_t;
class AActor;

namespace zx { namespace netsnapshot {

// Install the client-side mover sink; idempotent.
void MoverInstallSinks();

// DPlat::Tick's client counts.
void MoverCountCrushed();
void MoverCountPlatWaitTick();
void MoverCountPlatWaitExpired();

// Whether this client's legacy handler for the class must stand aside, records driving
// it instead; false on a server and whenever the gate is not suppressing that class.
bool MoverSuppressesLegacy( BYTE cls );
// The same, for a legacy handler's own test: counts the call it turns away.
bool MoverLegacyStandsAside( BYTE cls );
// The gate word SnapMoverGate carried, which the server sends at level 3 alone: read
// outside the movers too, by the sound guard that must know records carry those sounds.
bool ServerGatesLegacy( );
// A mover one of this client's legacy handlers built, which a mover gate-on drops: the
// records then rebuild it, and without the mark the two would both own the plane.
void MoverMadeByLegacy( class DMover * pMover );
// SnapMoverGate: whether the server stopped its legacy mover state, and the
// record floor (0 leaves it), forgotten at disconnect.
void MoverSetServerGate( bool bLegacyOff, int frame );
void MoverForgetServerGate( );
void MoverCountBlockedRecv();
// Whether this client must leave the plane where the server's record says it stalled,
// counted as one held tic; a server always moves it.
bool MoverHoldsPlane( bool bBlocked );
// The same for a two-plane mover, whose record carries a stall per plane: which planes
// to hold, with the held tic and the one-plane case counted here rather than at the call.
void MoverHeldPlanes( BYTE blocked, bool & bFloor, bool & bCeiling );
// What the mover's stall flag becomes: the server decides it from the move it just
// made, and a client keeps what the record told it.
bool MoverPlaneStalled( bool bWas, bool bStalled );
BYTE MoverPlanesStalled( BYTE was, bool bFloor, bool bCeiling );

void MoverCountHeld();
// A held tic on which a two-plane mover held only one plane.
void MoverCountHeldOnePlane();
// A plane a blocked record moved; `off` in fixed units.
void MoverCountStallPlane( unsigned int off );
void MoverCountRestPlane( BYTE cls, unsigned int off );
void MoverApplyGonePlane( sector_t * pSec, int plane, BYTE cls, fixed_t planeD );

}} // namespace zx::netsnapshot

// Sized here because p_spec.h does not include format.h;
// movers.cpp asserts this covers SNAPMV_COUNT.
#define SNAPMV_STATCLASSES	16

// The client's mover gate.
struct SnapMoverGateStats
{
	bool			bGate;
	int				floor;			// 0 when none applies
	unsigned int	dropped[2][SNAPMV_STATCLASSES];	// [legacy-made, record-made] by class
	unsigned int	belowFloor;		// records refused below the floor
	unsigned int	keptApplied;	// polyobject records kept and applied at a gate-on
};

namespace zx { namespace netsnapshot {

void ClientMoverGateStats( SnapMoverGateStats & st, bool bReset );
void MoverCountPlaneSuppressed();
void MoverCountLinkSuppressed();
void MoverCountMidtexSuppressed();
// One call per write site, folding the reference's test and its count: a plane a record owns,
// or one linked to it, must not take an absolute write.
bool MoverSkipsPlaneWrite( sector_t *sec, bool bCeiling );
// The same for a 3D midtexture's y offset on such a plane, which records move.
bool MoverSkipsMidtexY( line_t *line );
// Client map reset window; see g_moverResetOpen.
void DestroySectorMovers();
void MoverResetBegin();
void MoverResetEnd();
bool MoverInReset();
// The reset count, the server's from DestroyAllSectorMovers or SnapJoin
// (replaying the hold) or a datagram's around its records.
void MoverSetResetCount( BYTE count );
void MoverBeginDatagram( BYTE resetCount );
void MoverEndDatagram();
void MoverForgetResetCount();
// Records dropped as counted before the client's reset count, this window.
unsigned int MoverBeforeResetRecords( bool bReset );
// Whether the snapshot path owns this sector's plane on this client.
bool MoverCeilingOwnsPlane( sector_t *sec );
bool MoverFloorOwnsPlane( sector_t *sec );
bool MoverPlatOwnsPlane( sector_t *sec );
bool MoverDoorOwnsPlane( sector_t *sec );
bool MoverElevatorOwnsPlane( sector_t *sec );
bool MoverPillarOwnsPlane( sector_t *sec );

// Whether this side plays a door's or plat's wait-end sound itself: a client does not, the
// server sending it, and only a mover a record has reached reaches that code on a client.
bool MoverPlaysWaitSound( );

// Whether this client works out a crusher's reversal for itself: only for a mover a record
// has reached, so a client the records do not drive keeps taking the server's word for it.
bool MoverClientDerivesReversal( bool bSeen );

// Whether this client leaves a door's countdown to the server: it runs its own only for a
// door a record has reached, which with the gate off is none of them.
bool MoverClientSkipsWait( bool bSeen );
void MoverCountDoorWait( bool bExpired );
void MoverCountDoorWaitEnded( int left );
bool MoverOwnsPlane( sector_t *sec, bool bCeiling );
// Before a legacy waggle takes `plane` (sector_t::floor or ceiling).
void MoverEvictForWaggle( sector_t * pSec, int plane );
// Moves a corrected plane's 3D midtextures and linked sectors; `move` as
// DMover::MoveAttached takes it.
void MoverMoveAttached( sector_t * pSec, fixed_t move, bool bCeiling, const char * who );
// A veto this client overrode, counted: a true predicate and an absent veto
// read the same from a log otherwise.
void MoverCountNoBlock();
// True on a client under the mover gate, counting the veto it discards: the server decides
// a block and sends SNAPMVF_BLOCKED. With the gate off a client keeps stock's veto.
bool MoverDiscardsSectorVeto();
bool MoverDiscardsAttachedVeto();
void MoverCountAttachedVeto();

}} // namespace zx::netsnapshot

// Window totals, reset on read, `noblock` and `attached` over every mover class
// and `crushed` over DPlat's alone.
struct SnapMoverClientStats
{
	unsigned int	applied;
	unsigned int	noblock;
	unsigned int	attached;
	unsigned int	crushed;
	unsigned int	gone;
	unsigned int	goneNoLocal;	// of those, no local mover to correct
	unsigned int	goneUnheldPlane;// of those, the plane moved to the GONE's
	unsigned int	gonePlanesStale;// GONEs applied with a plane refused as stale
	unsigned int	goneSuperseded;// of those, ending a predecessor already replaced
	unsigned int	noLevel;	// refused, no level loaded
	unsigned int	unusable;	// refused, class or length this build cannot use
	unsigned int	evicted;	// stale movers destroyed for a record
	unsigned int	goneEvicted;// the same, for a GONE; not counted as no local
	unsigned int	genEvicted;	// of both, the same class under another generation
	unsigned int	waggleEvicted;	// tracked occupants a legacy waggle replaced
	unsigned int	waitTicks;	// wait tics a CLIENT advanced locally
	unsigned int	waitEnded;	// waits a record ended mid-countdown
	unsigned int	waitResid;	// tics left at those, summed
	unsigned int	waitWorst;	// tics left at the worst of them
	unsigned int	waitExpired;// waits the local countdown ended itself
	unsigned int	doorWaitTicks;	// door wait tics a client counted down
	unsigned int	doorWaitExpired;// door waits that countdown ended itself
	unsigned int	doorWaitEnded;	// door waits a record ended within a tic
	unsigned int	doorWaitEarly;	// the same, more left: a re-press, or lag
	unsigned int	doorWaitEarlyWorst;// tics left at the worst of those
	unsigned int	derivedReversals;// crusher reversals a client derived itself
	unsigned int	blockedRecv;// records applied carrying SNAPMVF_BLOCKED
	unsigned int	held;		// tics the hold skipped a local MoveFloor
	unsigned int	heldOnePlane;	// of those, one plane of two
	unsigned int	stallPlane;	// planes a blocked record moved
	unsigned int	stallWorst;	// largest of those, fixed units
	unsigned int	legacySuppressed;// legacy handler calls the gate turned away
	unsigned int	restPlane[SNAPMV_STATCLASSES];	// at-rest corrections, per class
	unsigned int	restWorst[SNAPMV_STATCLASSES];	// largest of those, fixed units
	unsigned int	gonePlane[SNAPMV_STATCLASSES];	// planes taken from a gone record
	unsigned int	goneWorst[SNAPMV_STATCLASSES];	// largest of those, fixed units
	unsigned int	planeSuppressed;// legacy plane verifications skipped
	unsigned int	linkSuppressed;	// the same, for a plane linked to an owned one
	unsigned int	midtexSuppressed;// mid y offsets skipped on an owned plane's lines
	unsigned int	resetsOpened;	// client map reset windows opened, never reset
	unsigned int	resetsClosed;	// of those, closed by DestroyAllSectorMovers
	unsigned int	resetsAbandoned;// of those, ended by a reconnect or replaced
	bool			bResetOpen;		// one open now; open across reports is stuck
	unsigned int	resetCount;		// the generation this client has carried out
	bool			bHaveResetCount;	// whether it knows one in this stream
	unsigned int	holdIn;			// records deferred: in a window, before a reset count, or past it
	unsigned int	holdAhead;		// of those, counted past the client's reset count
	unsigned int	holdReplayed;	// deferred records applied after the reset
	unsigned int	holdDropped;	// deferred records a reconnect or the reset count ended
	unsigned int	holdCoalesced;	// deferred records a newer one on the plane covered
	unsigned int	holdNarrowed;	// held GONEs narrowed for a newer instance
	unsigned int	holdPeak;		// most entries held at once
	unsigned int	holdAdvanced;	// replayed movers ticked forward by their hold
	unsigned int	holdAdvanceTics;	// of those, the tics
	unsigned int	holdAdvanceWorst;	// the largest
	unsigned int	holdAdvanceCapped;	// holds past the cap, not advanced
	unsigned int	holdBound;		// what that can reach: 6 x numsectors
};

namespace zx { namespace netsnapshot {

void MoverClientStats( SnapMoverClientStats & st, bool bReset );
// Stale refusals this window, read and reset once a report for its two lines.
unsigned int MoverStaleRecords( bool bReset );

}} // namespace zx::netsnapshot

// A polyobject record's outcome, into the mover line's own counts so its
// records still add up.
enum { SNAPMV_OUT_APPLIED, SNAPMV_OUT_GONE, SNAPMV_OUT_UNUSABLE, SNAPMV_OUT_STALE };

namespace zx { namespace netsnapshot {

void MoverCountOutcome( int outcome );

}} // namespace zx::netsnapshot

// DoScroller's added fields, and the client's point-pusher sources.
struct SnapScrollPushClientStats
{
	unsigned int	scrollers;		// from DoScroller
	unsigned int	controlled;		// of those, with a last height
	unsigned int	accelerative;	// of those, with a velocity
	unsigned int	moving;			// of those, a nonzero one
	unsigned int	points;			// point pushers from DoPusher
	unsigned int	noSource;		// of those, sent without a source
	unsigned int	resolved;		// sources found the first time
	unsigned int	resolvedMirror;	// of those, mirrors
	unsigned int	reresolved;		// sources found again after going
	unsigned int	unresolved;		// tics a point pusher skipped, no source
	unsigned int	gone;			// sources dropped, their id reused
	unsigned int	replacedScrollers;	// destroyed for a full update's re-send
	unsigned int	replacedPushers;
	unsigned int	replacedWaggles;
	// The join's own command: what it carried and how each record was matched.
	unsigned int	stateChunks;		// commands received
	unsigned int	stateScrollers;		// scroller records applied by ordinal
	unsigned int	statePushers;		// pusher records applied by ordinal
	unsigned int	stateEchoMismatch;	// records whose ordinal held another mover
	unsigned int	stateFallback;		// of those, matched by searching instead
	unsigned int	stateUnmatched;		// records no mover claimed, left at zero
};

// The join state command's records, defined with the command in
// network/servercommands.h; the command hands them over, as its own members are protected.
namespace ServerCommands { struct SnapScrollerState; struct SnapPusherState; }

namespace zx { namespace netsnapshot {

void SpecialCountScroller( bool bControl, bool bAccel, bool bMoving );
void SpecialCountPointPusher( bool bSource );
void SpecialClientStats( SnapScrollPushClientStats &st, bool bReset );
AActor *SpecialResolveSource( unsigned short &netID, bool &bSeen );
void SpecialClearForFullUpdate( );

// The join's scroller and pusher state, sent after both of SERVER_UpdateMovers' loops so
// every DoScroller and DoPusher has run on the client; the role and gate tests are inside.
void SpecialSendJoinState( unsigned int client );

// One chunk of that state applied to the movers the legacy commands built, by join ordinal
// with the record's own type and affectee as the check.
void SpecialApplyJoinState( int firstScroller,
	const TArray<struct ServerCommands::SnapScrollerState> &scrollers, int firstPusher,
	const TArray<struct ServerCommands::SnapPusherState> &pushers );

// Whether this line is attached to a snapshot-owned plane.
bool Midtex3dLineOwned(line_t *line);

// The join's plane order and the planes links drive; returns the sectors
// on or behind a link cycle.
int LinkWriteOrder(int *order, BYTE *drivenPlanes);
// Whether a control plane driving this sector's plane is snapshot-owned.
bool LinkControlOwned(sector_t *sector, bool ceiling);

// Reports the sector link graph and the edges an index-ordered join pass would
// write out of order, behind fua_snap_linkcensus.
void LinkCensus( void );

}} // namespace zx::netsnapshot

// p_linkedsectors.cpp's LINK_ plane bits: LINK_CEILINGBITS or LINK_FLOORBITS.
int P_LinkPlaneBits(bool ceiling);

#endif
