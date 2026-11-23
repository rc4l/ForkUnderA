// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: polyobj.h
//
// Description: The snapshot path's polyobjects: the server's capture,
// the client's record sink and its counts. Included by p_spec.h.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_POLYOBJ_H
#define ZX_NETSNAPSHOT_POLYOBJ_H

#include "basictypes.h"

class DPolyAction;

struct FPolyObj;

namespace zx { namespace netsnapshot {

// Polyobject records (design doc section 7.1).
void PolyCapture( FPolyObj *poly, bool bAllocate = true );	// server: the whole polyobject
void PolyApplyRecord( BYTE flags, WORD index, const BYTE *pBody,
	unsigned int bodyBytes, int frame );
void PolyDestroyAll();
bool OwnsPoly( FPolyObj *poly );
// At a mover gate-on, drops the legacy-made actions and applies the records
// kept while legacy was live; at a gate-off, drops the record-made ones and the
// kept records; each returns the actions dropped.
unsigned int PolyGateOn( unsigned int &applied );
unsigned int PolyGateOff( );
void PolyCountSuppressed();
// Ticks a polyobject's record-made, unblocked actions forward; how many.
unsigned int PolyAdvance( WORD index, int tics );

}} // namespace zx::netsnapshot

// Why a record set an axis, RESYNC being a perpetual rotator's rotation.
enum { SNAPPO_SET_FIRST, SNAPPO_SET_REST, SNAPPO_SET_BLOCKED, SNAPPO_SET_GONE,
	SNAPPO_SET_RESYNC, SNAPPO_SET_REASONS };

struct SnapPolyClientStats
{
	unsigned int	records;	// routed to the polyobject sink
	unsigned int	created;	// actions a record made
	unsigned int	evicted;	// record-made actions a record no longer listed
	unsigned int	gone;
	unsigned int	goneNoLocal;	// of those, with no record-made action
	unsigned int	stale;
	unsigned int	unusable;	// no such polyobject, or a body the walk refused
	unsigned int	ungated;	// ignored: the polyobject legacy handlers are live
	unsigned int	held;		// tics an action held while blocked
	unsigned int	done;		// tics an action held, its countdown run out
	unsigned int	set[SNAPPO_SET_REASONS];	// axes a record moved, by reason
	unsigned int	setWorstMove;	// largest translation, fixed units
	unsigned int	setWorstTurn;	// largest rotation, angle units
	unsigned int	resyncs;		// records re-anchoring a perpetual rotator
	unsigned int	resyncWorst;	// largest of their corrections, angle units
	unsigned int	suppressed;	// legacy position and rotation writes skipped
};

namespace zx { namespace netsnapshot {

void PolyClientStats( SnapPolyClientStats &st, bool bReset );

// The polyobject's own list of actions, kept by DPolyAction.
// A new action's generation, 0 on a client: the role test belongs here, not at the one site.
BYTE PolyNewGen();
// The sounds stock left to the client's own handler: a rotate and a plain move start the
// sequence locally and send none, so a gated client would turn in silence.
void PolySendStartSound( int polyNum );
// The sound stock never stopped, for a gated client whose own sequence would play on.
void PolySendStopSound( int polyNum );
void PolyLink( FPolyObj *poly, DPolyAction *pAction );
void PolyUnlink( FPolyObj *poly, DPolyAction *pAction );

}} // namespace zx::netsnapshot

#endif
