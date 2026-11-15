//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: clienttrack.cpp
//
// Description: Per-client snapshot state, the acknowledgement fold and
// repair, the priority scheduler, branch events, and the delta build: layout,
// chunking and encoding. See clienttrack.h for the model.
//
//-----------------------------------------------------------------------------

#include <string.h>
#include <math.h>

#include "doomdef.h"
#include "doomstat.h"
#include "features/net-snapshot/clienttrack.h"
#include "features/net-snapshot/capture.h"		// BranchedThisTic / BranchCensus
#include "features/net-snapshot/computation/ack_compute.h"
#include "features/net-snapshot/computation/budget_compute.h"
#include "i_system.h"		// I_MSTime, for the join fill timer

using namespace zx::netsnapshot;

//*****************************************************************************
//	STATE

// One entity as sent in a pending frame, its full post-state, which an ack
// folds by the groups written.
struct SnapSentEntity
{
	WORD				id;
	bool				left;		// this record was a leave tombstone
	BYTE				sentMask;	// groups actually carried
	bool				entered;	// an enter: SNAPGRP_ENTER is not in sentMask
	unsigned int		offset;		// where its record starts in the payload
	SnapEntityState		state;
};

// One psprite record as sent, in a list of its own, being a handful a frame.
struct SnapSentPSprite
{
	WORD				player;
	unsigned int		offset;		// where its record starts in the payload
	SnapPSpriteState	state;
};

// One mover record as sent, by seq, which is the whole dirty test.
struct SnapSentMover
{
	WORD				slot;
	WORD				seq;
	unsigned int		offset;		// where its record starts in the payload
};

// One datagram in flight, `seq` its delivery clock and `frame` its scheduling
// clock (sv_snapclient.md, The model).
struct SnapPendingFrame
{
	unsigned int			seq;
	unsigned int			frame;
	// An ack has seen its bit clear while it was inside SNAP_REORDER_WINDOW:
	// if it is proven delivered later, it arrived late rather than not at all.
	bool					sawUnknown;
	// Resolved lost by the ack being processed; repaired once its folds are done.
	bool					lost;
	// ...without proof, past the ack's history or behind a wider gap, so it
	// may have arrived.
	bool					lostUnproven;
	TArray<SnapSentEntity>	records;
	// Split across chunks by the same offsets, so a lost datagram costs exactly
	// the psprite rows it carried.
	TArray<SnapSentPSprite>	psprRecords;
	TArray<SnapSentMover>	moverRecords;
};

// One branch event waiting to go to one client, sent as blind copies and then
// dropped (sv_snapclient.md, Building a frame).
struct SnapPendingEvent
{
	SnapEntityState	state;		// the fields the event carries
	WORD			id;
	BYTE			type;
	BYTE			sends;		// copies transmitted so far
	unsigned int	queuedFrame;	// for the deadline: a count of DELIVERIES
	// The game tic it was observed on, which goes on the wire (9.2).
	unsigned int	queuedGameTic;
	unsigned int	sentFrame;		// frame the last copy went out on
	// A death (SNAPPRI_TERMINAL or the corpse bit), emitted first on the
	// terminal copies and spacing.
	bool			bTerminal;
};

struct SnapClientState
{
	bool					active;
	bool					haveAck;
	// The newest datagram acknowledged, not the newest frame.
	unsigned int			lastAckedSeq;
	// The datagrams declared lost within 32 of lastAckedSeq (ComputeLostMaskMark).
	unsigned int			lostMask;

	// The sequence the next datagram to this client carries, per client, from 1
	// (sv_main.cpp ignores an ack of 0), never rewound.
	unsigned int			nextSeq;

	// What the client is known to hold as of lastAckedSeq.
	SnapEntityState* believed;		// indexed by entity id

	// What the client will hold once everything in flight arrives, the delta's
	// reference, meaningful only where present[id] is set.
	SnapEntityState* sent;
	BYTE* present;		// parallel presence flags

	// A leave sent for this id and not yet folded, nor an enter after it, so an
	// entity back in the world meanwhile is sent enters (sv_snapclient.md).
	BYTE* leaving;
	unsigned int* leaveFrame;
	unsigned int			believedCount;

	// The join fill: how long a joining client spends in a world still
	// populating, reported once per fill.
	unsigned int			fillStartMS;	// 0 = not filling
	bool					fillReported;

	// Groups emitted since the newest ack, re-sent even when the value matches
	// once the pending depth plus a frame has passed.
	BYTE* unackedMask;

	// Psprite state per player slot, the sent and believed pair as for
	// entities, never suppressed.
	SnapPSpriteState* psprSent;
	SnapPSpriteState* psprBelieved;

	// Mover seqs per capture slot, the same pair, 0 meaning nothing sent, which
	// no live seq is.
	WORD					moverSentSeq[SNAPMV_MAXSLOTS];
	WORD					moverBelievedSeq[SNAPMV_MAXSLOTS];

	// Where the capped walk resumes, or it would serve the first slots forever.
	unsigned int			moverCursor;

	// Pending branch events, oldest first, bounded by the deadline.
	TArray<SnapPendingEvent>	events;

	// Where each band's walk resumes, advanced by the entries consumed, never
	// by time.
	unsigned int bandCursor[SNAPCL_TIERS_REPORT];

	// The frame each id's enter went out, stamped as its ack folds, which the
	// sound path's grace counts from.
	WORD* enterFrame;

	TArray<SnapPendingFrame> pending;
	unsigned int			scanStart;		// rotates so no entity is starved

	// Ids the client is believed to hold, compacted when stale entries
	// outnumber the live ones.
	TArray<WORD>			believedIds;
	BYTE* listed;		// id is in believedIds already

	// Ids whose enter rode a datagram lost without proof, which the client may
	// hold, so each is sent a leave once out of the world (sv_snapclient.md).
	BYTE* maybe;
	TArray<WORD>			maybeIds;
	WORD* lastSentFrame;	// for the aging term (wraps harmlessly)

	// Whether this entity was culled or held at its previous classification,
	// the edge the re-entry census needs.
	BYTE * wasHeld;

	// How long since the client heard anything of it, bounding a hold, where
	// lastSentFrame is the starvation guard's clock.
	WORD* lastRefreshFrame;
	// The frame position last went out, which missile suppression ages its
	// prediction by.
	WORD* lastPosFrame;
	// Same clock for APPEAR: once a group can be withheld, every consumer of
	// "how stale is this" needs the group's own clock.
	WORD* lastAppearFrame;

	// Records the previous frame held as cosmetic, sizing the refresh quota.
	unsigned int lastCosmeticCount;
	// Held records owing appearance only; sizes the TIER_ANIMREFRESH quota.
	unsigned int lastAnimrefCount;
	unsigned int			staleIds;
};

static SnapClientState g_clients[MAXPLAYERS];
static bool g_snapclInitialized = false;

void zx::netsnapshot::ServerInit()
{
	if (g_snapclInitialized)
		return;

	memset(g_clients, 0, sizeof(g_clients));
	g_snapclInitialized = true;
}

static void snapcl_EnsureAllocated(SnapClientState& cl)
{
	if (cl.believed == NULL)
	{
		cl.believed = new SnapEntityState[SNAP_MAXID];
		cl.sent = new SnapEntityState[SNAP_MAXID];
		cl.present = new BYTE[SNAP_MAXID];
		cl.leaving = new BYTE[SNAP_MAXID];
		cl.leaveFrame = new unsigned int[SNAP_MAXID];
		cl.unackedMask = new BYTE[SNAP_MAXID];
		cl.listed = new BYTE[SNAP_MAXID];
		cl.maybe = new BYTE[SNAP_MAXID];
		cl.lastSentFrame = new WORD[SNAP_MAXID];
		cl.lastRefreshFrame = new WORD[SNAP_MAXID];
		cl.lastPosFrame = new WORD[SNAP_MAXID];
		cl.lastAppearFrame = new WORD[SNAP_MAXID];
		cl.enterFrame = new WORD[SNAP_MAXID];
		cl.wasHeld = new BYTE[SNAP_MAXID];
		cl.psprSent = new SnapPSpriteState[MAXPLAYERS];
		cl.psprBelieved = new SnapPSpriteState[MAXPLAYERS];
		memset(cl.psprSent, 0, MAXPLAYERS * sizeof(SnapPSpriteState));
		memset(cl.psprBelieved, 0, MAXPLAYERS * sizeof(SnapPSpriteState));
		memset(cl.present, 0, SNAP_MAXID);
		memset(cl.leaving, 0, SNAP_MAXID);
		memset(cl.leaveFrame, 0, SNAP_MAXID * sizeof(unsigned int));
		memset(cl.unackedMask, 0, SNAP_MAXID);
		memset(cl.listed, 0, SNAP_MAXID);
		memset(cl.maybe, 0, SNAP_MAXID);
		memset(cl.lastSentFrame, 0, SNAP_MAXID * sizeof(WORD));
		memset(cl.lastRefreshFrame, 0, SNAP_MAXID * sizeof(WORD));
		memset(cl.lastPosFrame, 0, SNAP_MAXID * sizeof(WORD));
		memset(cl.lastAppearFrame, 0, SNAP_MAXID * sizeof(WORD));
		memset(cl.enterFrame, 0, SNAP_MAXID * sizeof(WORD));
		memset(cl.wasHeld, 0, SNAP_MAXID);
		memset(cl.bandCursor, 0, sizeof(cl.bandCursor));
		cl.believedCount = 0;
		cl.fillStartMS = 0;
		cl.fillReported = false;
	}
}

// Drops everything believed of a client's world, for the resets and the forced
// re-baseline, whose caller marks what the client may still hold.
static void snapcl_ForgetWorld(SnapClientState& cl)
{
	memset(cl.present, 0, SNAP_MAXID);
	memset(cl.leaving, 0, SNAP_MAXID);
	memset(cl.unackedMask, 0, SNAP_MAXID);
	memset(cl.listed, 0, SNAP_MAXID);
	memset(cl.maybe, 0, SNAP_MAXID);
	cl.maybeIds.Clear();

	// Queued events name ids that IDList::clear hands to other actors (9.2).
	cl.events.Clear();

	// Stale stamps would put a fresh baseline in TIER_OVERDUE, not ENTER.
	memset(cl.lastSentFrame, 0, SNAP_MAXID * sizeof(WORD));
	memset(cl.lastRefreshFrame, 0, SNAP_MAXID * sizeof(WORD));
	memset(cl.lastPosFrame, 0, SNAP_MAXID * sizeof(WORD));
	memset(cl.lastAppearFrame, 0, SNAP_MAXID * sizeof(WORD));
	if (cl.wasHeld != NULL)
		memset(cl.wasHeld, 0, SNAP_MAXID);

	// Zero is nothing sent, so every mover goes again, from the first slot.
	memset(cl.moverSentSeq, 0, sizeof(cl.moverSentSeq));
	memset(cl.moverBelievedSeq, 0, sizeof(cl.moverBelievedSeq));
	cl.moverCursor = 0;

	// Zero is weaponClass 0, i.e. "no weapon", so the first capture after a
	// forget differs for anyone holding one and re-sends without a special case.
	if (cl.psprSent != NULL)
		memset(cl.psprSent, 0, MAXPLAYERS * sizeof(SnapPSpriteState));
	if (cl.psprBelieved != NULL)
		memset(cl.psprBelieved, 0, MAXPLAYERS * sizeof(SnapPSpriteState));

	// Enter stamps have a map's lifetime too (9.2).
	memset(cl.enterFrame, 0, SNAP_MAXID * sizeof(WORD));

	cl.believedCount = 0;
	cl.believedIds.Clear();
	cl.staleIds = 0;
	cl.pending.Clear();
	cl.haveAck = false;
}

void zx::netsnapshot::ResetClient(unsigned int client)
{
	if (client >= MAXPLAYERS)
		return;

	SnapClientState& cl = g_clients[client];
	snapcl_EnsureAllocated(cl);

	snapcl_ForgetWorld(cl);
	cl.lastCosmeticCount = 0;
	cl.lastAnimrefCount = 0;
	cl.lastAckedSeq = 0;
	// Never rewound, since the client's high-water mark survives a map change
	// and a reconnect.
	if (cl.nextSeq == 0)
		cl.nextSeq = 1;
	cl.scanStart = 0;
	cl.active = true;

	// A reconnecting client is a new join, measured again.
	cl.fillStartMS = 0;
	cl.fillReported = false;
}

bool zx::netsnapshot::Believes(unsigned int client, unsigned int id)
{
	if ((client >= MAXPLAYERS) || (id >= SNAP_MAXID))
		return false;
	const SnapClientState& cl = g_clients[client];
	return (cl.present != NULL) && (cl.present[id] != 0);
}

// Frames since this client began believing in `id`, or 0, wrapping harmlessly
// every 31 minutes.
unsigned int zx::netsnapshot::BelievedFrames(unsigned int client, unsigned int id, unsigned int nowFrame)
{
	if ((client >= MAXPLAYERS) || (id >= SNAP_MAXID))
		return 0;
	const SnapClientState& cl = g_clients[client];
	if ((cl.present == NULL) || (cl.present[id] == 0) || (cl.enterFrame == NULL))
		return 0;
	return (unsigned int)(WORD)((WORD)nowFrame - cl.enterFrame[id]);
}

// The mover table was reset (capture stopped), so no slot a client holds
// survives: nothing sent or believed, and no pending frame's rows.
void zx::netsnapshot::ForgetMovers()
{
	for (unsigned int i = 0; i < MAXPLAYERS; ++i)
	{
		SnapClientState& cl = g_clients[i];
		memset(cl.moverSentSeq, 0, sizeof(cl.moverSentSeq));
		memset(cl.moverBelievedSeq, 0, sizeof(cl.moverBelievedSeq));
		cl.moverCursor = 0;
		for (unsigned int f = 0; f < cl.pending.Size(); ++f)
			cl.pending[f].moverRecords.Clear();
	}
}

void zx::netsnapshot::ResetAllClients()
{
	for (unsigned int i = 0; i < MAXPLAYERS; ++i)
	{
		// Only slots ever used: allocating the per-client tables for every
		// empty slot at each map change is not worth it.
		if (g_clients[i].believed != NULL)
			ResetClient(i);
	}
}

//*****************************************************************************
//	ACKNOWLEDGEMENT

// Enters whose loss was unproven (see `maybe`): ids marked, leaves sent for
// them once out of the world, and marks a confirmed enter cleared.
static unsigned int g_maybeMarked = 0, g_maybeLeaves = 0, g_maybeClearedEnter = 0;

static void snapcl_ApplyFrame(SnapClientState& cl, const SnapPendingFrame& pf)
{
	for (unsigned int i = 0; i < pf.records.Size(); ++i)
	{
		const SnapSentEntity& rec = pf.records[i];
		if (rec.left)
		{
			if (cl.present[rec.id])
			{
				cl.present[rec.id] = 0;
				--cl.believedCount;
				++cl.staleIds;		// entry stays in believedIds until compaction
			}
			cl.unackedMask[rec.id] = 0;
			cl.maybe[rec.id] = 0;
			// The newest leave folds, so the marker is done (an older one's
			// must not clear a newer leave's).
			if (cl.leaving[rec.id] && (pf.frame >= cl.leaveFrame[rec.id]))
				cl.leaving[rec.id] = 0;
		}
		else
		{
			// An enter sent after the leave clears it, one sent before not.
			if (rec.entered && cl.leaving[rec.id] && (pf.frame > cl.leaveFrame[rec.id]))
				cl.leaving[rec.id] = 0;
			if (cl.present[rec.id] == 0)
			{
				cl.present[rec.id] = 1;
				if (cl.maybe[rec.id])
				{
					cl.maybe[rec.id] = 0;
					++g_maybeClearedEnter;
				}
				// The moment belief begins, which is what the sound path
				// measures its grace window from.
				cl.enterFrame[rec.id] = (WORD)pf.frame;
				++cl.believedCount;
				if (cl.listed[rec.id] == 0)
				{
					cl.listed[rec.id] = 1;
					cl.believedIds.Push(rec.id);
				}
			}
			// Only the groups written, or withheld ones would read as held.
			SnapEntityState & bel = cl.believed[rec.id];
			if ((rec.sentMask & SNAPGRP_POS) != 0)
			{
				bel.qx = rec.state.qx;
				bel.qy = rec.state.qy;
			}
			if ((rec.sentMask & SNAPGRP_POSZ) != 0)
				bel.qz = rec.state.qz;
			if ((rec.sentMask & SNAPGRP_ANG) != 0)
			{
				bel.qangle = rec.state.qangle;
				bel.qpitch = rec.state.qpitch;
			}
			if ((rec.sentMask & SNAPGRP_VELMISC) != 0)
			{
				bel.qvx = rec.state.qvx;
				bel.qvy = rec.state.qvy;
				bel.qvz = rec.state.qvz;
				bel.misc = rec.state.misc;
				bel.effects = rec.state.effects;
			}
			if ((rec.sentMask & SNAPGRP_APPEAR) != 0)
			{
				bel.qtics = rec.state.qtics;
				bel.qstate = rec.state.qstate;
			}
			if ((rec.sentMask & SNAPGRP_RENDER) != 0)
				bel.renderstate = rec.state.renderstate;
			bel.id = rec.state.id;
			bel.classId = rec.state.classId;
		}
	}

	// Psprite rows carried by the same datagram, whole, as they are sent.
	for (unsigned int i = 0; i < pf.psprRecords.Size(); ++i)
	{
		const SnapSentPSprite & pr = pf.psprRecords[i];
		if (pr.player < MAXPLAYERS)
			cl.psprBelieved[pr.player] = pr.state;
	}

	// Mover rows likewise, on the seq the record CARRIED: the slot may have
	// transitioned since, and the live value would mark that delivered too.
	for (unsigned int i = 0; i < pf.moverRecords.Size(); ++i)
	{
		const SnapSentMover & mr = pf.moverRecords[i];
		if (mr.slot < SNAPMV_MAXSLOTS)
			cl.moverBelievedSeq[mr.slot] = mr.seq;
	}
}

// Frames the server sent, confirmed delivered, and confirmed LOST; the ack's
// history bits are what make loss provable.
static unsigned int g_ackFramesSeen = 0;
static unsigned int g_ackFramesLost = 0;
static unsigned int g_ackGapsWide = 0;	// gap wider than the 32-frame window
static unsigned int g_acksAhead = 0;	// naming a datagram not yet sent, ignored

unsigned int zx::netsnapshot::AcksAhead(bool reset)
{
	const unsigned int n = g_acksAhead;
	if (reset)
		g_acksAhead = 0;
	return n;
}
// Frames folded on history evidence rather than as the acked frame.
static unsigned int g_ackFoldedExtra = 0;
// Of the confirmed, those an earlier ack had seen missing: reordered, not lost.
static unsigned int g_ackFramesLate = 0;
// Bits a later ack set for a datagram already declared lost: one that arrived
// after it was, or one declared lost without proof that had arrived.
static unsigned int g_ackSetAfterLost = 0;
// Enters sent to an id still believed present, because its leave is in flight.
static unsigned int g_reentersInFlight = 0;
static unsigned int g_targetEnters = 0;
// Optimistic references walked back because their frame was proven lost.
static unsigned int g_repairedFrames = 0, g_repairedEntities = 0;
// Counted apart from entities: movers are a few records against hundreds, so
// a nonzero frame count says nothing about whether a slot was reverted.
static unsigned int g_repairedMovers = 0;

void zx::netsnapshot::RepairStats(unsigned int& frames, unsigned int& entities,
	unsigned int& movers, bool reset)
{
	frames = g_repairedFrames;
	entities = g_repairedEntities;
	movers = g_repairedMovers;
	if (reset)
		g_repairedFrames = g_repairedEntities = g_repairedMovers = 0;
}

void zx::netsnapshot::MaybeStats(unsigned int& marked, unsigned int& leaves,
	unsigned int& clearedEnter, bool reset)
{
	marked = g_maybeMarked;
	leaves = g_maybeLeaves;
	clearedEnter = g_maybeClearedEnter;
	if (reset)
		g_maybeMarked = g_maybeLeaves = g_maybeClearedEnter = 0;
}

void zx::netsnapshot::AckLossStats(unsigned int& seen, unsigned int& lost, unsigned int& wide,
	unsigned int& folded, unsigned int& late, unsigned int& setAfterLost, bool reset)
{
	seen = g_ackFramesSeen;
	lost = g_ackFramesLost;
	wide = g_ackGapsWide;
	folded = g_ackFoldedExtra;
	late = g_ackFramesLate;
	setAfterLost = g_ackSetAfterLost;
	if (reset)
		g_ackFramesSeen = g_ackFramesLost = g_ackGapsWide = g_ackFoldedExtra
			= g_ackFramesLate = g_ackSetAfterLost = 0;
}

// Enters forced by a leave in flight, and `onPresent`, the leave markers on ids
// still believed present now, over every client (not a window total).
void zx::netsnapshot::PresenceStats(unsigned int& reenters, unsigned int& onPresent, bool reset)
{
	reenters = g_reentersInFlight;
	onPresent = 0;
	for (unsigned int c = 0; c < MAXPLAYERS; ++c)
	{
		const SnapClientState& cl = g_clients[c];
		if ((cl.active == false) || (cl.leaving == NULL))
			continue;
		for (unsigned int i = 0; i < cl.believedIds.Size(); ++i)
		{
			const WORD id = cl.believedIds[i];
			if (cl.present[id] && cl.leaving[id])
				++onPresent;
		}
	}
	if (reset)
		g_reentersInFlight = 0;
}

void zx::netsnapshot::PresenceLog(unsigned int client, unsigned int frame)
{
	if (client >= MAXPLAYERS)
		return;
	const SnapClientState& cl = g_clients[client];
	if ((cl.active == false) || (cl.present == NULL) || (cl.leaving == NULL)
		|| (cl.maybe == NULL))
		return;

	static TArray<BYTE> s_seen;
	if (s_seen.Size() != SNAP_MAXID)
		s_seen.Resize(SNAP_MAXID);
	memset(&s_seen[0], 0, SNAP_MAXID);
	TArray<WORD> ids;
	for (unsigned int k = 0; k < cl.believedIds.Size(); ++k)
	{
		const WORD id = cl.believedIds[k];
		if ((cl.present[id] || cl.leaving[id]) && (s_seen[id] == 0))
		{
			s_seen[id] = 1;
			ids.Push(id);
		}
	}
	for (unsigned int k = 0; k < cl.maybeIds.Size(); ++k)
	{
		const WORD id = cl.maybeIds[k];
		if ((cl.maybe[id] || cl.leaving[id]) && (s_seen[id] == 0))
		{
			s_seen[id] = 1;
			ids.Push(id);
		}
	}
	for (unsigned int p = 0; p < cl.pending.Size(); ++p)
	{
		const SnapPendingFrame& pf = cl.pending[p];
		for (unsigned int r = 0; r < pf.records.Size(); ++r)
		{
			const WORD id = pf.records[r].id;
			if (pf.records[r].entered && (s_seen[id] == 0))
			{
				s_seen[id] = 1;
				ids.Push(id);
			}
		}
	}

	FILE* f = fopen("snappresence-sv.txt", "a");
	if (f == NULL)
		return;
	fprintf(f, "sv %u %u %u", frame, client, ids.Size());
	for (unsigned int k = 0; k < ids.Size(); ++k)
		fprintf(f, " %u", (unsigned int)ids[k]);
	fprintf(f, "\n");
	fclose(f);
}

void zx::netsnapshot::TargetStats(unsigned int& enters, bool reset)
{
	enters = g_targetEnters;
	if (reset)
		g_targetEnters = 0;
}

// Ids and players repaired by the ack being processed, so their references
// can be rebuilt; cleared by the rebuild.
static BYTE g_repairMark[SNAP_MAXID];
static TArray<WORD> g_repairIds;
static bool g_repairPlayer[MAXPLAYERS];

// After a repair, rebuilds each repaired entity's `sent` as `believed` with
// every record still in flight merged over in order, and each repaired
// psprite's as its newest in flight, else `psprBelieved`.
static void snapcl_RebuildRepaired(SnapClientState& cl, unsigned int firstInFlight)
{
	for (unsigned int f = firstInFlight; f < cl.pending.Size(); ++f)
	{
		const SnapPendingFrame& pf = cl.pending[f];
		for (unsigned int r = 0; r < pf.records.Size(); ++r)
		{
			const SnapSentEntity& rec = pf.records[r];
			if (rec.left || (g_repairMark[rec.id] == 0))
				continue;
			MergeGroups(cl.sent[rec.id], ServerMapFrame(), rec.state, rec.sentMask);
		}
		for (unsigned int r = 0; r < pf.psprRecords.Size(); ++r)
		{
			const SnapSentPSprite& prec = pf.psprRecords[r];
			if ((prec.player < MAXPLAYERS) && g_repairPlayer[prec.player])
				cl.psprSent[prec.player] = prec.state;
		}
	}

	for (unsigned int i = 0; i < g_repairIds.Size(); ++i)
		g_repairMark[g_repairIds[i]] = 0;
	g_repairIds.Clear();
	memset(g_repairPlayer, 0, sizeof(g_repairPlayer));
}

// The reference check after every ack (sv_snapclient.md): each client's `sent`
// against `believed` with the records in flight merged over.
CVAR(Bool, fua_snap_debug_refcheck, false, CVAR_NOSETBYACS)
static unsigned int g_refInFlight = 0, g_refInFlightDiffer = 0;
static unsigned int g_refAtRest = 0, g_refAtRestDiffer = 0;
static unsigned int g_refPsprites = 0, g_refPspritesDiffer = 0;
static WORD g_refSlot[SNAP_MAXID];			// 1 + index into g_refExpect, 0 none
static TArray<SnapEntityState> g_refExpect;
static TArray<WORD> g_refIds;

void zx::netsnapshot::ReferenceCheckStats(unsigned int& inFlight, unsigned int& inFlightDiffer,
	unsigned int& atRest, unsigned int& atRestDiffer, unsigned int& psprites,
	unsigned int& pspritesDiffer, bool reset)
{
	inFlight = g_refInFlight;
	inFlightDiffer = g_refInFlightDiffer;
	atRest = g_refAtRest;
	atRestDiffer = g_refAtRestDiffer;
	psprites = g_refPsprites;
	pspritesDiffer = g_refPspritesDiffer;
	if (reset)
	{
		g_refInFlight = g_refInFlightDiffer = g_refAtRest = g_refAtRestDiffer = 0;
		g_refPsprites = g_refPspritesDiffer = 0;
	}
}

// Whether two states read the same to the checksum: its fields, pitch as the
// wire carries it and misc without the checksum's own bits.
static bool snapcl_SameHeld(const SnapEntityState& a, const SnapEntityState& b)
{
	return (a.qx == b.qx) && (a.qy == b.qy) && (a.qz == b.qz) && (a.qangle == b.qangle)
		&& (WirePitch(a.qpitch) == WirePitch(b.qpitch))
		&& (a.qvx == b.qvx) && (a.qvy == b.qvy) && (a.qvz == b.qvz)
		&& (a.qstate == b.qstate) && (a.renderstate == b.renderstate)
		&& (a.effects == b.effects)
		&& ((a.misc & SNAP_MISC_CHECKED) == (b.misc & SNAP_MISC_CHECKED));
}

static void snapcl_CheckReferences(const SnapClientState& cl)
{
	// In flight: `believed` with every pending record merged over in order,
	// the leave tombstones aside.
	for (unsigned int f = 0; f < cl.pending.Size(); ++f)
	{
		const SnapPendingFrame& pf = cl.pending[f];
		for (unsigned int r = 0; r < pf.records.Size(); ++r)
		{
			const SnapSentEntity& rec = pf.records[r];
			if (rec.left)
				continue;
			if (g_refSlot[rec.id] == 0)
			{
				g_refExpect.Push(cl.believed[rec.id]);
				g_refIds.Push(rec.id);
				g_refSlot[rec.id] = (WORD)g_refExpect.Size();
			}
			MergeGroups(g_refExpect[g_refSlot[rec.id] - 1], ServerMapFrame(), rec.state,
				rec.sentMask);
		}
	}
	for (unsigned int i = 0; i < g_refIds.Size(); ++i)
	{
		++g_refInFlight;
		if (snapcl_SameHeld(cl.sent[g_refIds[i]], g_refExpect[i]) == false)
			++g_refInFlightDiffer;
	}

	// At rest: present with nothing in flight, so `sent` is `believed`.
	for (unsigned int i = 0; i < cl.believedIds.Size(); ++i)
	{
		const WORD id = cl.believedIds[i];
		if ((cl.present[id] == 0) || (g_refSlot[id] != 0))
			continue;
		++g_refAtRest;
		if (snapcl_SameHeld(cl.sent[id], cl.believed[id]) == false)
			++g_refAtRestDiffer;
	}

	for (unsigned int i = 0; i < g_refIds.Size(); ++i)
		g_refSlot[g_refIds[i]] = 0;
	g_refIds.Clear();
	g_refExpect.Clear();

	// Psprites: the newest record in flight, else `psprBelieved`.
	if ((cl.psprSent == NULL) || (cl.psprBelieved == NULL))
		return;
	for (unsigned int pl = 0; pl < MAXPLAYERS; ++pl)
	{
		const SnapPSpriteState* pWant = &cl.psprBelieved[pl];
		for (unsigned int f = 0; f < cl.pending.Size(); ++f)
		{
			const SnapPendingFrame& pf = cl.pending[f];
			for (unsigned int r = 0; r < pf.psprRecords.Size(); ++r)
			{
				if (pf.psprRecords[r].player == pl)
					pWant = &pf.psprRecords[r].state;
			}
		}
		// The fields the psprite's dirty test compares.
		++g_refPsprites;
		if ((pWant->weaponClass != cl.psprSent[pl].weaponClass)
			|| (pWant->qstate[0] != cl.psprSent[pl].qstate[0])
			|| (pWant->qstate[1] != cl.psprSent[pl].qstate[1]))
			++g_refPspritesDiffer;
	}
}

void zx::netsnapshot::OnAck(unsigned int client, unsigned int seq, unsigned int history)
{
	if (client >= MAXPLAYERS)
		return;

	SnapClientState& cl = g_clients[client];
	if (cl.active == false)
		return;

	// An older ack carries less than one already processed, and one ahead of
	// what was sent would make every true ack stale (ComputeAckRead).
	const SnapAckRead rd = ComputeAckRead(cl.haveAck, cl.lastAckedSeq, seq, cl.nextSeq);
	if (rd.bAhead)
		++g_acksAhead;
	if (rd.bStale || rd.bAhead)
		return;

	if (rd.bWide)
		++g_ackGapsWide;

	// A bit set for a datagram already declared lost, counted once.
	cl.lostMask = ComputeLostMaskAdvance(cl.lostMask, cl.haveAck, cl.lastAckedSeq, seq);
	g_ackSetAfterLost += ComputeSetAfterLost(cl.lostMask, history);
	cl.lostMask &= ~history;

	const bool canReadBits = rd.bCanReadBits;

	// Resolves pending datagrams in sequence order, stopping at the first one
	// that may only be late (sv_snapclient.md, The model).
	unsigned int resolved = 0;
	unsigned int newestFolded = 0;
	bool haveFolded = false;
	for (unsigned int i = 0; i < cl.pending.Size(); ++i)
	{
		SnapPendingFrame& pf = cl.pending[i];
		const int verdict = ComputeAckVerdict(seq, history, canReadBits, pf.seq);
		if (verdict == SNAP_ACK_AHEAD)
			break;

		if (verdict == SNAP_ACK_DELIVERED)
		{
			snapcl_ApplyFrame(cl, pf);
			++g_ackFramesSeen;
			if (pf.seq != seq)
				++g_ackFoldedExtra;
			if (pf.sawUnknown)
				++g_ackFramesLate;
			newestFolded = i;
			haveFolded = true;
		}
		else if (verdict == SNAP_ACK_WAIT)
		{
			pf.sawUnknown = true;
			break;
		}
		else
		{
			pf.lost = true;
			pf.lostUnproven = (verdict == SNAP_ACK_LOST_UNPROVEN);
			cl.lostMask = ComputeLostMaskMark(cl.lostMask, seq, pf.seq);
			++g_ackFramesSeen;
			++g_ackFramesLost;
		}
		++resolved;
	}

	// Repair: each entity a lost datagram carried reverts whole to `believed`,
	// then is rebuilt over what is still in flight.
	bool bRepaired = false;
	for (unsigned int i = 0; i < resolved; ++i)
	{
		const SnapPendingFrame& pf = cl.pending[i];
		if (pf.lost == false)
			continue;
		for (unsigned int r = 0; r < pf.records.Size(); ++r)
		{
			const WORD id = pf.records[r].id;
			cl.sent[id] = cl.believed[id];
			++g_repairedEntities;
			if (g_repairMark[id] == 0)
			{
				g_repairMark[id] = 1;
				g_repairIds.Push(id);
			}
			bRepaired = true;
			// An enter that may have arrived, for an id not believed present.
			if (pf.lostUnproven && pf.records[r].entered && (cl.present[id] == 0)
				&& (cl.maybe[id] == 0))
			{
				cl.maybe[id] = 1;
				cl.maybeIds.Push(id);
				++g_maybeMarked;
			}
		}
		// Same reversion for psprites, or the next diff would be taken against a
		// state the client was never told.
		for (unsigned int r = 0; r < pf.psprRecords.Size(); ++r)
		{
			const WORD pl = pf.psprRecords[r].player;
			if (pl < MAXPLAYERS)
			{
				cl.psprSent[pl] = cl.psprBelieved[pl];
				g_repairPlayer[pl] = true;
				bRepaired = true;
			}
		}
		// Movers too, the reverted seq re-sending the whole body next frame.
		for (unsigned int r = 0; r < pf.moverRecords.Size(); ++r)
		{
			const WORD sl = pf.moverRecords[r].slot;
			if (sl < SNAPMV_MAXSLOTS)
			{
				cl.moverSentSeq[sl] = cl.moverBelievedSeq[sl];
				++g_repairedMovers;
			}
		}
		++g_repairedFrames;
	}
	if (bRepaired)
		snapcl_RebuildRepaired(cl, resolved);

	// Cleared, not rebuilt, since `sent` describes what is in flight.
	memset(cl.unackedMask, 0, SNAP_MAXID);

	if (resolved > 0)
	{
		TArray<SnapPendingFrame> remaining;
		for (unsigned int i = resolved; i < cl.pending.Size(); ++i)
			remaining.Push(cl.pending[i]);
		cl.pending = remaining;
	}

	cl.haveAck = true;
	cl.lastAckedSeq = seq;
	if (fua_snap_debug_refcheck)
		snapcl_CheckReferences(cl);
}

//*****************************************************************************
//	PRIORITY SCORING (design doc section 5.2)
//
// The bands, their constants and the classifier are snap_interest_compute's;
// this file reads the settings and the view, counts and drains.

// The interest cull's distance, past which an entity is demoted, never omitted.
EXTERN_CVAR(Int, fua_snap_cull_dist)
EXTERN_CVAR(Int, fua_snap_cull_fov)

// 0 all enters to ENTER, 1 always distance-graded, 2 graded only in the fill.
EXTERN_CVAR(Int, fua_snap_entergrade)

// Missile priority, split from the interest cull; see the cvar definitions in
// capture.cpp.
EXTERN_CVAR(Int, fua_snap_missile_dist)
EXTERN_CVAR(Int, fua_snap_bandreserve)

// Terminal promotion takes any flagged record carrying APPEAR, position too.

// The error in map units the server lets the client's own extrapolation of a
// ballistic missile reach before its position is sent (0: off).
CVAR(Int, fua_snap_missile_predict, 16, CVAR_NOSETBYACS)

// Server-side appearance suppression, a toggle for A/B runs.
CVAR(Bool, fua_snap_appear_predict, true, CVAR_NOSETBYACS)

// Honours the control group, off only to price what it costs.
CVAR(Bool, fua_snap_appear_exempt, true, CVAR_NOSETBYACS)

// Counted at the decision, the complete population, unlike arriving records.
static unsigned int g_apHeld[SNAPCL_APOP_COUNT] = { 0 };		// withheld
static unsigned int g_apDiverged[SNAPCL_APOP_COUNT] = { 0 };	// landed elsewhere
static unsigned int g_apUnwalkable[SNAPCL_APOP_COUNT] = { 0 };	// no route

// Names live beside the counters and the report derives its columns from them,
// so a population added here cannot be silently dropped from the line.
const char * const SNAPCL_AppearPopNames[] = { "player", "other" };

typedef char snapcl_assert_apopnames[
	(sizeof(SNAPCL_AppearPopNames) / sizeof(SNAPCL_AppearPopNames[0])
		== SNAPCL_APOP_COUNT) ? 1 : -1];

// Reserved ids are playernum + 1 (3.1), so this is a comparison rather than a
// lookup and cannot be wrong about which entity it is counting.
static inline unsigned int snapcl_AppearPop(unsigned int id)
{
	return ((id > 0) && (id <= (unsigned int)MAXPLAYERS))
		? (unsigned int)SNAPCL_APOP_PLAYER : (unsigned int)SNAPCL_APOP_OTHER;
}

// Suppression outcomes, held and sent apart, the two halves of the tolerance.
static unsigned int g_predHeld = 0;		// POS withheld
static unsigned int g_predSent = 0;		// error crossed the tolerance
static unsigned int g_predExpired = 0;	// past SNAP_BALLISTIC_MAX_TICS
static unsigned int g_predWorst = 0;	// worst error withheld, map units
// The gravity class (design doc 5.2).
static unsigned int g_lawHeld = 0, g_lawSentPos = 0, g_lawSentVel = 0, g_lawExpired = 0, g_lawWorst = 0;

// cos(fua_snap_cull_fov / 2) cached for the cull, -1 or below for no cone.
double g_cullHalfCos = -1.0;
void zx::netsnapshot::UpdateCullFov(int fovDegrees)
{
	if (fovDegrees >= 360)
	{
		g_cullHalfCos = -1.0;			// never out-of-view by angle
		return;
	}
	if (fovDegrees < 1)
		fovDegrees = 1;
	const double half = (double)fovDegrees * 0.5 * (3.14159265358979 / 180.0);
	g_cullHalfCos = cos(half);
}

// The appearance refresh interval in tics, as SNAPCL_AGE_COSMETIC is for
// position, clamped above 0, which the quota divides by.
CUSTOM_CVAR(Int, fua_snap_animref_interval, TICRATE * 33 / 10, CVAR_NOSETBYACS)
{
	if (self < 1)
		self = 1;
}

// The ceiling on the refresh pass's cost, as a divisor of the budget, binding
// only on a large held set.
#define SNAPCL_REFRESH_CAP_DIV		2

static unsigned int g_lastTierCount[SNAPCL_TIERS];
// Entities ranked into each band, against g_lastTierCount's records sent.
static unsigned int g_lastTierSize[SNAPCL_TIERS];
// The reserve's accounting: bands stopped by their ceiling (`capped`) and
// those that ran out of entities first (`drained`).
static unsigned int g_lastReserveAllowed = 0, g_lastReserveSpent = 0;
static unsigned int g_lastReserveCapped = 0, g_lastReserveDrained = 0;
static unsigned int g_lastDeferred = 0;
static unsigned int g_lastForced = 0;
static unsigned int g_lastFresh = 0;
static unsigned int g_lastResend = 0;
static unsigned int g_lastResendBytes = 0;

// Forced re-baselines this window, a client SNAPCL_MAX_PENDING frames behind.
static unsigned int g_rebaselines = 0;

// Contested-set counts by distance ring and in front, summed over a stats
// window; GetCensus reports them.
static unsigned int g_censusByDist[SNAPCL_DIST_BUCKETS] = { 0 };
static unsigned int g_censusFrontByDist[SNAPCL_DIST_BUCKETS] = { 0 };
static unsigned int g_censusContested = 0;
static unsigned int g_censusFront = 0;
static unsigned int g_lastCosmetic = 0;

// Records carrying SNAPPRI_TERMINAL, and what became of the cosmetic
// exemption's claim on them.
static unsigned int g_terminalSeen = 0;
static unsigned int g_terminalPromoted = 0;	// classified into TIER_ENTER
static unsigned int g_terminalCapped = 0;	// wanted promotion, hit the ceiling
static unsigned int g_terminalEmitted = 0;	// actually written to a packet

// Reset per frame; the ceiling is a per-frame record count.
static unsigned int g_terminalThisFrame = 0;

// Records for entities serviceable again after a hold, by lastRefreshFrame's
// silence, which the guard cannot see.
static unsigned int g_reentrySeen = 0;
// Buckets span seconds: a held entity sits on a multi-second clock, so it
// cannot return having been silent for much less than one.
static unsigned int g_reentryHist[5] = { 0, 0, 0, 0, 0 };	// <35, <105, <210, <420, 420+ tics

// What the contested set owes, by group and by wait, position owed being the
// real queue.
static unsigned int g_owedByGroup[SNAP_GROUP_COUNT] = { 0 };
static unsigned int g_owedStale[4] = { 0, 0, 0, 0 };
static unsigned int g_owedPosStale[4] = { 0, 0, 0, 0 };
static unsigned int g_owedTotal = 0;

void zx::netsnapshot::OwedStats(unsigned int* byGroup, unsigned int* stale, unsigned int* posStale, unsigned int& total, bool reset)
{
	for (unsigned int i = 0; i < SNAP_GROUP_COUNT; ++i)
		byGroup[i] = g_owedByGroup[i];

	for (unsigned int i = 0; i < 4; ++i)
	{
		stale[i] = g_owedStale[i];
		posStale[i] = g_owedPosStale[i];
	}
	total = g_owedTotal;
	if (reset)
	{
		memset(g_owedByGroup, 0, sizeof(g_owedByGroup));
		memset(g_owedStale, 0, sizeof(g_owedStale));
		memset(g_owedPosStale, 0, sizeof(g_owedPosStale));
		g_owedTotal = 0;
	}
}

// Records classified into each band and how many carried POS.
static unsigned int g_bandRecs[SNAPCL_TIERS_REPORT] = { 0 };
static unsigned int g_bandPos[SNAPCL_TIERS_REPORT] = { 0 };

// One record's band from snap_interest_compute, with the settings in force,
// and the terminal counts it returns.
static unsigned int snapcl_Tier(const SnapEntityState& e, BYTE pri, BYTE mask,
	const SnapViewpoint& view, double viewCos, double viewSin, unsigned int age,
	unsigned int refreshAge, bool& forced, bool& cosmetic, bool& culled,
	unsigned int& distBucket, bool& inFront, bool fillMode, bool transition)
{
	SnapTierIn in;
	in.qx = e.qx;
	in.qy = e.qy;
	in.pri = pri;
	in.mask = mask;
	in.bView = view.valid;
	in.viewX = view.x;
	in.viewY = view.y;
	in.viewCos = viewCos;
	in.viewSin = viewSin;
	in.age = age;
	in.refreshAge = refreshAge;
	in.bFillMode = fillMode;
	in.bTransition = transition;
	in.enterGrade = fua_snap_entergrade;
	in.missileDist = fua_snap_missile_dist;
	in.cullDist = fua_snap_cull_dist;
	in.cullHalfCos = g_cullHalfCos;
	in.terminalThisFrame = g_terminalThisFrame;
	SnapTierOut out;
	const unsigned int tier = ComputeTier(in, out);
	if (out.terminal != SNAP_TERMINAL_NONE)
		++g_terminalSeen;
	if (out.terminal == SNAP_TERMINAL_PROMOTED)
	{
		++g_terminalThisFrame;
		++g_terminalPromoted;
	}
	else if (out.terminal == SNAP_TERMINAL_CAPPED)
		++g_terminalCapped;
	forced = out.bForced;
	cosmetic = out.bCosmetic;
	culled = out.bCulled;
	distBucket = out.distBucket;
	inFront = out.bInFront;
	return tier;
}

unsigned int zx::netsnapshot::LastTierCount(unsigned int tier)
{
	return (tier < SNAPCL_TIERS) ? g_lastTierCount[tier] : 0;
}

unsigned int zx::netsnapshot::LastTierSize(unsigned int tier)
{
	return (tier < SNAPCL_TIERS) ? g_lastTierSize[tier] : 0;
}

unsigned int zx::netsnapshot::LastReserveAllowed()	{ return g_lastReserveAllowed; }
unsigned int zx::netsnapshot::LastReserveSpent()		{ return g_lastReserveSpent; }
unsigned int zx::netsnapshot::LastReserveCapped()		{ return g_lastReserveCapped; }
unsigned int zx::netsnapshot::LastReserveDrained()	{ return g_lastReserveDrained; }

static unsigned int g_lastAnimrefWanted = 0;
static unsigned int g_lastAnimrefQuota = 0;

void zx::netsnapshot::AnimrefQuota(unsigned int& quota, unsigned int& wanted, unsigned int& population)
{
	quota = g_lastAnimrefQuota;
	wanted = g_lastAnimrefWanted;
	population = (MAXPLAYERS > 0) ? g_clients[0].lastAnimrefCount : 0;
}

unsigned int zx::netsnapshot::LastDeferred()
{
	return g_lastDeferred;
}

unsigned int zx::netsnapshot::LastFresh()
{
	return g_lastFresh;
}

unsigned int zx::netsnapshot::LastResend()
{
	return g_lastResend;
}

unsigned int zx::netsnapshot::LastResendBytes()
{
	return g_lastResendBytes;
}

unsigned int zx::netsnapshot::LastForced()
{
	return g_lastForced;
}

void zx::netsnapshot::TerminalStats(unsigned int& seen, unsigned int& promoted,
	unsigned int& capped, unsigned int& emitted, bool reset)
{
	seen = g_terminalSeen;
	promoted = g_terminalPromoted;
	capped = g_terminalCapped;
	emitted = g_terminalEmitted;

	if (reset)
		g_terminalSeen = g_terminalPromoted = g_terminalCapped = g_terminalEmitted = 0;
}

void zx::netsnapshot::BandComposition(unsigned int* recs, unsigned int* pos, bool reset)
{
	for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
	{
		recs[t] = g_bandRecs[t];
		pos[t] = g_bandPos[t];
	}
	if (reset)
	{
		memset(g_bandRecs, 0, sizeof(g_bandRecs));
		memset(g_bandPos, 0, sizeof(g_bandPos));
	}
}

void zx::netsnapshot::ReentryStats(unsigned int& seen, unsigned int* hist, bool reset)
{
	seen = g_reentrySeen;
	for (unsigned int i = 0; i < 5; ++i)
		hist[i] = g_reentryHist[i];

	if (reset)
	{
		g_reentrySeen = 0;
		memset(g_reentryHist, 0, sizeof(g_reentryHist));
	}
}

void zx::netsnapshot::AppearStats(unsigned int * held, unsigned int * diverged,
	unsigned int * unwalkable, bool reset)
{
	for (unsigned int i = 0; i < SNAPCL_APOP_COUNT; ++i)
	{
		held[i] = g_apHeld[i];
		diverged[i] = g_apDiverged[i];
		unwalkable[i] = g_apUnwalkable[i];
	}
	if (reset)
	{
		memset(g_apHeld, 0, sizeof(g_apHeld));
		memset(g_apDiverged, 0, sizeof(g_apDiverged));
		memset(g_apUnwalkable, 0, sizeof(g_apUnwalkable));
	}
}

void zx::netsnapshot::PredictStats(unsigned int& held, unsigned int& sent,
	unsigned int& expired, unsigned int& worst, bool reset)
{
	held = g_predHeld;
	sent = g_predSent;
	expired = g_predExpired;
	worst = g_predWorst;
	if (reset)
		g_predHeld = g_predSent = g_predExpired = g_predWorst = 0;
}

void zx::netsnapshot::ServerLawStats(unsigned int& held, unsigned int& sentPos, unsigned int& sentVel,
	unsigned int& expired, unsigned int& worst, bool reset)
{
	held = g_lawHeld;
	sentPos = g_lawSentPos;
	sentVel = g_lawSentVel;
	expired = g_lawExpired;
	worst = g_lawWorst;
	if (reset)
		g_lawHeld = g_lawSentPos = g_lawSentVel = g_lawExpired = g_lawWorst = 0;
}

unsigned int zx::netsnapshot::LastCosmetic()
{
	return g_lastCosmetic;
}

//*****************************************************************************
//	DELTA CONSTRUCTION

//*****************************************************************************
//	BRANCH EVENTS

// Blind copies of each event on consecutive frames: two cost twice the bytes
// and turn 5% loss into 0.25%.
CVAR(Int, fua_snap_event_copies, 1, CVAR_ARCHIVE | CVAR_NOSETBYACS)

// Deaths take SNAPCL_EVENT_TERMINAL_COPIES copies spaced apart, since a burst
// drops consecutive frames together.

// The share of a frame's budget the missile band may take, so a plasma burst
// cannot starve every monster (`bands capped` on the reserve line).
CVAR(Int, fua_snap_missile_share, 20, CVAR_ARCHIVE | CVAR_NOSETBYACS)

// Records a frame the refresh band may take (0: no cap), past which its
// interval stretches rather than the near bands losing records.
CVAR(Int, fua_snap_refresh_max, 40, CVAR_NOSETBYACS)

// Records the refresh band asked for when the clamp cut it; 0 when it did not.
static unsigned int g_lastRefreshWanted = 0;

// Kept apart: an event that never got budget is a different failure from one
// sent and lost, and only the first says the share is too small.
static unsigned int g_evQueued = 0;		// accepted into a client's queue
static unsigned int g_evSent = 0;		// first transmission
static unsigned int g_evRetried = 0;	// any copy after the first
static unsigned int g_evExpUnsent = 0;	// deadline hit having NEVER been sent
static unsigned int g_evExpUnacked = 0;	// sent at least once, then expired
static unsigned int g_evDroppedFull = 0;	// queue cap hit
// Events replaced in the queue before any copy was sent.
static unsigned int g_evSupUnsent = 0;	// replaced before any copy went out
static unsigned int g_evSupSent = 0;	// replaced after delivery - not a loss
static unsigned int g_evBytes = 0;		// bytes actually spent on events

// Why the emit loop passed over an event, per visit rather than per event.
static unsigned int g_evSkipDone = 0;	// all copies already out
static unsigned int g_evSkipSameFrame = 0;	// one copy per frame rule
static unsigned int g_evSkipShare = 0;	// share exhausted: the real "too small"
static unsigned int g_evVisits = 0;		// loop iterations, to size the rest

// Terminal events apart, a missed death being visible where a flinch is not.
static unsigned int g_evTermSent = 0;
static unsigned int g_evTermExpUnsent = 0;
static unsigned int g_evTermRetried = 0;	// spaced copies after the first
// Deaths dropped with copies unsent because the entity's leave went out, none
// sent being a loss and some sent a removal cutting them short.
static unsigned int g_evDroppedLeftUnsent = 0;
static unsigned int g_evDroppedLeftPartial = 0;

// What the emitter used, summed over the window's emits with events queued,
// so the event emit line spans the same emits (9.2).
static unsigned int g_evEmits = 0;
static QWORD		g_evBudgetSum = 0;	// budgetBytes as received
static QWORD		g_evShareSum = 0;	// shares derived from them
static QWORD		g_evQueueSum = 0;	// queue depth at each emit
static unsigned int g_evQueueMax = 0;
static unsigned int g_evSpentMax = 0;	// one emit's spend, at most its share
static unsigned int g_evBreaks = 0;		// emits the share stopped

void zx::netsnapshot::ServerEventStats(unsigned int& queued, unsigned int& sent, unsigned int& retried,
	unsigned int& expUnsent, unsigned int& expUnacked, unsigned int& droppedFull,
	unsigned int& bytes, unsigned int& supUnsent, unsigned int& supSent)
{
	queued = g_evQueued; sent = g_evSent; retried = g_evRetried;
	expUnsent = g_evExpUnsent; expUnacked = g_evExpUnacked;
	droppedFull = g_evDroppedFull; bytes = g_evBytes;
	supUnsent = g_evSupUnsent; supSent = g_evSupSent;
}

void zx::netsnapshot::EventEmitStats(unsigned int& emits, QWORD& budgetSum, QWORD& shareSum,
	unsigned int& spentMax, QWORD& queueSum, unsigned int& queueMax, unsigned int& breaks)
{
	emits = g_evEmits;
	budgetSum = g_evBudgetSum;
	shareSum = g_evShareSum;
	spentMax = g_evSpentMax;
	queueSum = g_evQueueSum;
	queueMax = g_evQueueMax;
	breaks = g_evBreaks;
}

void zx::netsnapshot::EventTerminals(unsigned int& sent, unsigned int& expUnsent,
	unsigned int& retried, unsigned int& leftUnsent, unsigned int& leftPartial)
{
	sent = g_evTermSent;
	expUnsent = g_evTermExpUnsent;
	retried = g_evTermRetried;
	leftUnsent = g_evDroppedLeftUnsent;
	leftPartial = g_evDroppedLeftPartial;
}

void zx::netsnapshot::EventSkips(unsigned int& visits, unsigned int& done,
	unsigned int& sameFrame, unsigned int& share)
{
	visits = g_evVisits; done = g_evSkipDone;
	sameFrame = g_evSkipSameFrame; share = g_evSkipShare;
}

void zx::netsnapshot::ServerEventStatsReset()
{
	g_evQueued = g_evSent = g_evRetried = 0;
	g_evExpUnsent = g_evExpUnacked = g_evDroppedFull = g_evBytes = 0;
	g_evSupUnsent = g_evSupSent = 0;
	g_evSkipDone = g_evSkipSameFrame = g_evSkipShare = g_evVisits = 0;
	g_evTermSent = g_evTermExpUnsent = 0;
	g_evTermRetried = g_evDroppedLeftUnsent = g_evDroppedLeftPartial = 0;
	g_evEmits = g_evQueueMax = g_evSpentMax = g_evBreaks = 0;
	g_evBudgetSum = g_evShareSum = g_evQueueSum = 0;
}

// A safety net sized for a group waking together, since the deadline bounds the
// queue, so `dropped full` above 0 is a fault.
#define SNAPCL_EVENT_QUEUE_MAX	2048

// Psprite records one client may be sent in a frame, a valve for every player
// changing weapon state at once.
#define SNAPCL_PSPRITE_MAX		SNAPCL_PSPRITE_MAX_REPORT

static unsigned int g_psprEmitted = 0;	// records written, all clients
static unsigned int g_psprCapped = 0;	// client-frames that hit the valve

static unsigned int g_moverEmitted = 0;	// records written, all clients
static unsigned int g_moverCapped = 0;	// client-frames that hit the ceiling
// Those written with SNAPMVF_BLOCKED, apart from the capture's count.
static unsigned int g_moverEmitBlocked = 0;
// Per class, and the stair shape apart, so a log can say whether the second
// floor shape was ever written.
static unsigned int g_moverEmitCls[SNAPMV_COUNT] = { 0 };
static unsigned int g_moverEmitStair = 0;
// The pass has TWO exits, the ceiling and the byte budget; each is counted.
static unsigned int g_moverOutOfBytes = 0;

void zx::netsnapshot::ServerMoverStats(unsigned int & emitted, unsigned int & capped,
	unsigned int & emitBlocked, unsigned int & stair, unsigned int * perCls,
	unsigned int & outOfBytes, bool reset)
{
	emitted = g_moverEmitted;
	capped = g_moverCapped;
	emitBlocked = g_moverEmitBlocked;
	stair = g_moverEmitStair;
	outOfBytes = g_moverOutOfBytes;
	for (int i = 0; i < SNAPMV_COUNT; i++)
		perCls[i] = g_moverEmitCls[i];
	if (reset)
	{
		g_moverEmitted = g_moverCapped = g_moverEmitBlocked = g_moverEmitStair = 0;
		g_moverOutOfBytes = 0;
		memset(g_moverEmitCls, 0, sizeof(g_moverEmitCls));
	}
}

bool zx::netsnapshot::MoverAcked(unsigned int client, unsigned int slot, WORD seq)
{
	if ((client >= MAXPLAYERS) || (slot >= SNAPMV_MAXSLOTS))
		return true;
	return g_clients[client].moverBelievedSeq[slot] == seq;
}

// Acks of 0 are ignored and sequences start at 1, so a nonzero is an ack.
bool zx::netsnapshot::StreamStarted(unsigned int client)
{
	if (client >= MAXPLAYERS)
		return false;
	return g_clients[client].lastAckedSeq != 0;
}

BYTE zx::netsnapshot::AckLag(unsigned int client, unsigned int seq)
{
	if (client >= MAXPLAYERS)
		return SNAP_ACK_LAG_NONE;
	const SnapClientState& cl = g_clients[client];
	return ComputeAckLag(cl.haveAck, cl.lastAckedSeq, seq);
}

void zx::netsnapshot::ServerPSpriteStats(unsigned int & emitted, unsigned int & capped, bool reset)
{
	emitted = g_psprEmitted;
	capped = g_psprCapped;
	if (reset)
		g_psprEmitted = g_psprCapped = 0;
}

// Encoding census over the EMITTED stream, behind fua_snap_wirecensus. The
// capture's "dirty groups" line counts what was available to send, before
// the per-client pass rations anything; this counts what shipped, where the
// bytes are written (section 9.4).
//   per band  - group frequency, group bytes, record count and total bytes
//   apponly   - records whose only group is APPEAR
//   idgap     - gaps between emitted ids once sorted, bucketed by varint width
//   qstate    - (depth, offset) over emitted APPEAR, and whether qtics fits in
//               seven bits: the narrow form's reach
CVAR(Bool, fua_snap_wirecensus, false, CVAR_NOSETBYACS)

#define SNAPCL_CENSUS_IDBUCKETS	3	// varint width a sorted-id gap would need

struct SnapWireCensus
{
	QWORD	recs[SNAPCL_TIERS_REPORT];
	QWORD	bytes[SNAPCL_TIERS_REPORT];
	QWORD	appOnly[SNAPCL_TIERS_REPORT];
	QWORD	grpRecs[SNAP_GROUP_COUNT];
	QWORD	grpBytes[SNAP_GROUP_COUNT];
	QWORD	enters;
	QWORD	headerBytes;
	QWORD	idGap[SNAPCL_CENSUS_IDBUCKETS];
	QWORD	idGapSum;
	QWORD	idGapN;
	QWORD	chunkGap[SNAPCL_CENSUS_IDBUCKETS];
	QWORD	chunkGapSum;
	QWORD	chunkGapN;
	QWORD	chunkFirst;
	// APPEAR's narrow-form reach over every emitted APPEAR group, by the
	// joint condition an encoder takes.
	QWORD	qsAppear;			// every emitted APPEAR, resolvable or not
	QWORD	qsUnresolved;		// qstate 0xFFFF: always wide
	QWORD	qsNarrow8;			// depth 0 AND offset < 256 AND qtics < 128
	QWORD	qsNarrow7;			// depth 0 AND offset < 128 AND qtics < 128
	void Reset() { memset(this, 0, sizeof(*this)); }
};
static SnapWireCensus g_census;

// Ids emitted this frame with their byte offsets, which split them at the
// chunker's cuts for the gap census.
struct SnapCensusId
{
	WORD			id;
	unsigned int	offset;
};
static TArray<SnapCensusId> g_censusIds;

// Which pending row a descriptor owns, none for events and the local block.
enum
{
	SNAPEMIT_ENTITY = 0,
	SNAPEMIT_PSPRITE,
	SNAPEMIT_MOVER,
	SNAPEMIT_NOROW
};

// One emitted record, encoded but not yet laid out, its bytes carried so the
// drain and the layout agree on its length.
struct SnapEmitRec
{
	// The sent row travels with the record, to land in its chunk's entry.
	SnapSentEntity	sent;		// SNAPEMIT_ENTITY
	SnapSentPSprite	psent;		// SNAPEMIT_PSPRITE
	SnapSentMover	msent;		// SNAPEMIT_MOVER
	WORD			id;
	BYTE			kind;
	// Body bytes, mask and payload, the id written at encode after the cut.
	BYTE			len;
	bool			census;		// drain records only; the prefix is not tiered
	BYTE			bytes[SNAP_MAX_EMIT_BYTES];
	// The census needs the record's final payload offset to know which chunk
	// it landed in, so that is taken at layout too.
	unsigned int	censusTier;
	BYTE			censusMask;
	SnapEntityState	censusState;
};

// Everything emitted ahead of the ranked entity records, in order, as
// descriptors for the layout to write.
static TArray<SnapEmitRec> g_prefixRecs;
static TArray<SnapEmitRec> g_emitRecs;

// The bytes the prefix will take, each record charged a raw id, since the gap
// is not known until layout.
static unsigned int g_prefixBytes = 0;

// Stages one prefix record and returns it for the caller's pending row, `n`
// bounded at compile time by SNAP_MAX_EMIT_BYTES.
static SnapEmitRec & snapcl_PushPrefix(const BYTE * p, unsigned int n,
	WORD id, BYTE kind)
{
	SnapEmitRec er;
	er.id = id;
	er.kind = kind;
	er.len = (BYTE)n;
	er.census = false;
	er.censusTier = 0;
	er.censusMask = 0;
	for (unsigned int b = 0; b < n; ++b)
		er.bytes[b] = p[b];
	g_prefixRecs.Push(er);
	g_prefixBytes += n + SNAP_REC_IDBYTES_RAW;
	return g_prefixRecs[g_prefixRecs.Size() - 1];
}

// `idBytes` is what the encoder actually spent on this record's id; the
// width varies per record.
static void snapcl_CensusRecord(unsigned int tier, WORD id, BYTE mask,
	unsigned int bytes, unsigned int idBytes, unsigned int offset,
	const SnapEntityState & st)
{
	if (tier >= SNAPCL_TIERS_REPORT)
		return;
	++g_census.recs[tier];
	g_census.bytes[tier] += bytes;
	g_census.headerBytes += idBytes + SNAP_REC_MASK_BYTES;
	if (mask & SNAPGRP_ENTER)
		++g_census.enters;

	static const BYTE bits[SNAP_GROUP_COUNT] =
	{
		SNAPGRP_POS, SNAPGRP_POSZ, SNAPGRP_ANG, SNAPGRP_VELMISC,
		SNAPGRP_APPEAR, SNAPGRP_RENDER
	};
	for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
	{
		if (mask & bits[g])
		{
			++g_census.grpRecs[g];
			g_census.grpBytes[g] += GroupBytesFor(ServerMapFrame(), bits[g], st);
		}
	}

	if ((mask & SNAPGRP_GROUPMASK) == SNAPGRP_APPEAR)
		++g_census.appOnly[tier];

	// The narrow form needs depth, offset and qtics small in the same record,
	// so the joint is counted.
	if (mask & SNAPGRP_APPEAR)
	{
		++g_census.qsAppear;
		if (st.qstate == 0xFFFF)
		{
			++g_census.qsUnresolved;
		}
		else
		{
			const unsigned int depth = (st.qstate >> SNAP_STATE_DEPTH_SHIFT) & 7;
			const unsigned int offset = st.qstate & SNAP_STATE_OFFSET_MASK;
			const bool bTics = (st.qtics < 128);
			if ((depth == 0) && (offset < 256) && bTics)
				++g_census.qsNarrow8;
			if ((depth == 0) && (offset < 128) && bTics)
				++g_census.qsNarrow7;
		}
	}

	SnapCensusId ci;
	ci.id = id;
	ci.offset = offset;
	g_censusIds.Push(ci);
}

// Once per client frame after the cut, buckets the sorted-id gaps over the
// frame and within each chunk the encoder produced.
static void snapcl_CensusFrameEnd(const TArray<SnapChunk> & chunks)
{
	const unsigned int n = g_censusIds.Size();
	if (n > 1)
	{
		// Insertion sort by id: the set is a few hundred and already close to
		// ordered, since the priority walk carries a persistent cursor.
		for (unsigned int i = 1; i < n; ++i)
		{
			const SnapCensusId v = g_censusIds[i];
			unsigned int j = i;
			while ((j > 0) && (g_censusIds[j - 1].id > v.id))
			{
				g_censusIds[j] = g_censusIds[j - 1];
				--j;
			}
			g_censusIds[j] = v;
		}
		for (unsigned int i = 1; i < n; ++i)
		{
			const unsigned int gap =
				(unsigned int)(g_censusIds[i].id - g_censusIds[i - 1].id);
			g_census.idGapSum += gap;
			++g_census.idGapN;
			++g_census.idGap[(gap < 128) ? 0 : ((gap < 16384) ? 1 : 2)];
		}
	}

	// Per chunk, each opener's raw id counted.
	for (unsigned int c = 0; c < chunks.Size(); ++c)
	{
		const unsigned int lo = chunks[c].offset;
		const unsigned int hi = lo + chunks[c].length;
		unsigned int prev = 0;
		bool have = false;
		// g_censusIds is sorted by id, so this walk is in id order within the
		// chunk, the order the encoder emits.
		for (unsigned int i = 0; i < n; ++i)
		{
			if ((g_censusIds[i].offset < lo) || (g_censusIds[i].offset >= hi))
				continue;
			if (have == false)
			{
				++g_census.chunkFirst;	// pays a raw id
				prev = g_censusIds[i].id;
				have = true;
				continue;
			}
			const unsigned int gap = (unsigned int)(g_censusIds[i].id - prev);
			prev = g_censusIds[i].id;
			g_census.chunkGapSum += gap;
			++g_census.chunkGapN;
			++g_census.chunkGap[(gap < 128) ? 0 : ((gap < 16384) ? 1 : 2)];
		}
	}
	g_censusIds.Clear();
}

void zx::netsnapshot::WireCensus(SnapWireCensusOut & out, bool reset)
{
	memset(&out, 0, sizeof(out));
	for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
	{
		out.recs[t] = g_census.recs[t];
		out.bytes[t] = g_census.bytes[t];
		out.appOnly[t] = g_census.appOnly[t];
	}
	for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
	{
		out.grpRecs[g] = g_census.grpRecs[g];
		out.grpBytes[g] = g_census.grpBytes[g];
	}
	out.enters = g_census.enters;
	out.headerBytes = g_census.headerBytes;
	for (unsigned int i = 0; i < SNAPCL_CENSUS_IDBUCKETS; ++i)
		out.idGap[i] = g_census.idGap[i];
	out.idGapSum = g_census.idGapSum;
	out.idGapN = g_census.idGapN;
	for (unsigned int i = 0; i < SNAPCL_CENSUS_IDBUCKETS; ++i)
		out.chunkGap[i] = g_census.chunkGap[i];
	out.chunkGapSum = g_census.chunkGapSum;
	out.chunkGapN = g_census.chunkGapN;
	out.chunkFirst = g_census.chunkFirst;
	out.qsAppear = g_census.qsAppear;
	out.qsUnresolved = g_census.qsUnresolved;
	out.qsNarrow8 = g_census.qsNarrow8;
	out.qsNarrow7 = g_census.qsNarrow7;
	if (reset)
		g_census.Reset();
}

// Enter-record queue wait for a client's own missiles in tics, read against
// the client's milliseconds as a distribution.
#define SNAPCL_OWNENTER_BUCKETS	6
static unsigned int g_ownEnterHist[SNAPCL_OWNENTER_BUCKETS];
static unsigned int g_ownEnterN = 0;
static unsigned int g_ownEnterWorst = 0;

// The report prints six columns by hand.
typedef char snapcl_assert_ownenter_columns[
	(SNAPCL_OWNENTER_BUCKETS == 6) ? 1 : -1];

void zx::netsnapshot::OwnEnterStats(unsigned int * hist, unsigned int & samples,
	unsigned int & worst, bool reset)
{
	for (unsigned int i = 0; i < SNAPCL_OWNENTER_BUCKETS; ++i)
	{
		hist[i] = g_ownEnterHist[i];
		if (reset)
			g_ownEnterHist[i] = 0;
	}
	samples = g_ownEnterN;
	worst = g_ownEnterWorst;
	if (reset)
		g_ownEnterN = g_ownEnterWorst = 0;
}

// Local-block emission, `emitted` equalling client-frames while each client has
// a pawn, and `auth` read off the records written (10.1).
static unsigned int g_localEmitted = 0;		// records written, all clients
static unsigned int g_localAuth = 0;		// of those, marked authoritative
static unsigned int g_localNoPawn = 0;		// client had no pawn this frame
static unsigned int g_localSpectator = 0;	// excluded on purpose
static unsigned int g_localNoClient = 0;	// no CLIENT_s: should be zero
static unsigned int g_localNoId = 0;		// reserved id unusable: should be zero

void zx::netsnapshot::ServerLocalStats(SnapLocalEmitStats & out, bool reset)
{
	out.emitted = g_localEmitted;
	out.authoritative = g_localAuth;
	out.noPawn = g_localNoPawn;
	out.spectator = g_localSpectator;
	out.noClient = g_localNoClient;
	out.noId = g_localNoId;
	if (reset)
	{
		g_localEmitted = g_localAuth = g_localNoPawn = 0;
		g_localSpectator = g_localNoClient = g_localNoId = 0;
	}
}

// Queues a branch for one client, the caller having culled by distance.
void zx::netsnapshot::QueueEvent(unsigned int client, WORD id, BYTE type,
	const SnapEntityState& st, unsigned int frame, unsigned int gameTic, bool bTerminal)
{
	if (client >= MAXPLAYERS)
		return;
	SnapClientState& cl = g_clients[client];
	if (cl.active == false)
		return;

	// A second branch on the same entity supersedes the first.
	for (unsigned int i = 0; i < cl.events.Size(); ++i)
	{
		if ((cl.events[i].id == id) && (cl.events[i].type == type))
		{
			// Superseded, a loss only before any copy went out.
			if (cl.events[i].sends == 0)
				++g_evSupUnsent;
			else
				++g_evSupSent;
			cl.events[i].state = st;
			cl.events[i].queuedFrame = frame;
			cl.events[i].queuedGameTic = gameTic;
			cl.events[i].sends = 0;
			cl.events[i].bTerminal = bTerminal;
			return;
		}
	}

	if (cl.events.Size() >= SNAPCL_EVENT_QUEUE_MAX)
	{
		++g_evDroppedFull;
		return;
	}
	SnapPendingEvent ev;
	ev.state = st;
	ev.id = id;
	ev.type = type;
	ev.sends = 0;
	ev.queuedFrame = frame;
	ev.queuedGameTic = gameTic;
	ev.sentFrame = 0;
	ev.bTerminal = bTerminal;
	cl.events.Push(ev);
	++g_evQueued;
}

// Copies, spacing and deadline from snap_interest_compute.
static void snapcl_EventDelivery(const SnapPendingEvent& ev, unsigned int& copies,
	unsigned int& spacing, unsigned int& deadline)
{
	ComputeEventDelivery(ev.bTerminal, (int)fua_snap_event_copies, copies, spacing, deadline);
}

// Drop everything past its deadline, counting the two expiry causes apart.
static void snapcl_ExpireEvents(SnapClientState& cl, unsigned int frame)
{
	for (unsigned int i = 0; i < cl.events.Size(); )
	{
		const SnapPendingEvent& ev = cl.events[i];
		unsigned int copies, spacing, deadline;
		snapcl_EventDelivery(ev, copies, spacing, deadline);
		if ((frame - ev.queuedFrame) >= deadline)
		{
			if (ev.sends == 0)
			{
				++g_evExpUnsent;
				if (ev.bTerminal)
					++g_evTermExpUnsent;
			}
			else				++g_evExpUnacked;
			// Swap-with-last: order within a frame does not matter, and an
			// O(n^2) sweep here froze the server once (9.2).
			cl.events[i] = cl.events[cl.events.Size() - 1];
			cl.events.Pop();
			continue;
		}
		++i;
	}
}

// Stages pending events up to the share, before the priority passes, and
// returns the bytes used.
static unsigned int snapcl_WriteEvents(SnapClientState& cl,
	unsigned int budgetBytes, unsigned int frame)
{
	snapcl_ExpireEvents(cl, frame);

	// An entity whose leave is in this frame's prefix ends its copies, which
	// the client would drop as orphans.
	for (unsigned int i = 0; i < cl.events.Size(); )
	{
		const WORD id = cl.events[i].id;
		if ((cl.present[id] == 0) || cl.leaving[id])
		{
			// Deaths only, with copies unsent: one fully sent is merely
			// waiting out its deadline.
			unsigned int copies, spacing, deadline;
			snapcl_EventDelivery(cl.events[i], copies, spacing, deadline);
			if (cl.events[i].bTerminal && (cl.events[i].sends == 0))
				++g_evDroppedLeftUnsent;
			else if (cl.events[i].bTerminal && (cl.events[i].sends < copies))
				++g_evDroppedLeftPartial;
			cl.events[i] = cl.events[cl.events.Size() - 1];
			cl.events.Pop();
			continue;
		}
		++i;
	}
	if (cl.events.Size() == 0)
		return 0;

	const unsigned int share = (budgetBytes * SNAPCL_EVENT_SHARE_PCT) / 100;
	unsigned int spent = 0;
	++g_evEmits;
	g_evBudgetSum += budgetBytes;
	g_evShareSum += share;
	g_evQueueSum += cl.events.Size();
	if (cl.events.Size() > g_evQueueMax)
		g_evQueueMax = cl.events.Size();
	BYTE rec[SNAP_MAX_EVENT_BYTES];

	// Two passes, deaths first, since in a burst the share stops the loop and
	// the queue's order is scrambled (9.2).
	bool bShareStopped = false;
	for (int pass = 0; (pass < 2) && (bShareStopped == false); ++pass)
	for (unsigned int i = 0; i < cl.events.Size(); ++i)
	{
		SnapPendingEvent& ev = cl.events[i];
		const bool bTerminal = ev.bTerminal;
		if (bTerminal != (pass == 0))
			continue;			// counted on the pass that owns it, not twice
		++g_evVisits;
		unsigned int copies, spacing, deadline;
		snapcl_EventDelivery(ev, copies, spacing, deadline);
		if (ev.sends >= copies)
			{ ++g_evSkipDone; continue; }	// all copies out; expires on deadline
		if ((ev.sends > 0) && (frame < ev.sentFrame + spacing))
			{ ++g_evSkipSameFrame; continue; }	// one copy per frame or spacing,
												// never two in a datagram

		// queuedGameTic is when the branch was OBSERVED, which is what the
		// client needs, not when this copy leaves.
		const unsigned int n = WriteEventBody(rec, ServerMapFrame(),
			ev.type, ev.state, ev.queuedGameTic);

		if ((spent + n) > share)
		{
			// The eligible remainder over the whole array, which two passes
			// leave scattered.
			for (unsigned int k = 0; k < cl.events.Size(); ++k)
			{
				const SnapPendingEvent & tail = cl.events[k];
				unsigned int tc, tsp, tdl;
				snapcl_EventDelivery(tail, tc, tsp, tdl);
				if (tail.sends >= tc)
					continue;

				if ((tail.sends > 0) && (frame < tail.sentFrame + tsp))
					continue;

				++g_evSkipShare;
			}
			++g_evBreaks;
			bShareStopped = true;
			break;
		}

		snapcl_PushPrefix(rec, n, ev.id, SNAPEMIT_NOROW);
		NoteCosmeticRecord(ev.id, n + SNAP_REC_IDBYTES_RAW);
		spent += n;
		ev.sentFrame = frame;
		if (ev.sends == 0)
		{
			++g_evSent;
			if (bTerminal)
				++g_evTermSent;
		}
		else
		{
			++g_evRetried;
			if (bTerminal)
				++g_evTermRetried;
		}
		++ev.sends;
	}
	g_evBytes += spent;
	if (spent > g_evSpentMax)
		g_evSpentMax = spent;
	return spent;
}

// Every record's estimated start, ascending, then the payload's size.
static TArray<unsigned int> s_bounds;

// Emission order for one client frame, the prefix and then the sorted records.
static TArray<SnapEmitRec *> s_emit;

// The record that OPENS each chunk: what the cut walk returns instead of a
// byte offset, which would not survive the encode.
static TArray<unsigned int> s_cuts;

unsigned int zx::netsnapshot::BuildDelta(unsigned int client, const SnapEntityState* world,
	const BYTE* priority, unsigned int worldCount, unsigned int frame,
	unsigned int gameTic,
	TArray<SnapChunk>& chunks, TArray<BYTE>& out, unsigned int budgetBytes,
	unsigned int chunkBytes, const SnapViewpoint& view)
{
	out.Clear();
	chunks.Clear();
	s_bounds.Clear();
	g_prefixRecs.Clear();
	g_emitRecs.Clear();
	g_prefixBytes = 0;
	g_terminalThisFrame = 0;	// the promotion ceiling is per frame, per client
	if (client >= MAXPLAYERS)
		return 0;

	SnapClientState& cl = g_clients[client];
	if (cl.active == false)
		ResetClient(client);
	snapcl_EnsureAllocated(cl);

	// A client too far behind, by its oldest pending frame, is re-baselined.
	if ((cl.pending.Size() > 0)
		&& ((frame - cl.pending[0].frame) >= SNAPCL_MAX_PENDING))
	{
		// The client keeps its ghosts, so every id it may hold is marked
		// again after the forget, to be re-entered or sent a leave.
		static TArray<WORD> s_held;
		s_held.Clear();
		for (unsigned int k = 0; k < cl.believedIds.Size(); ++k)
		{
			const WORD id = cl.believedIds[k];
			if (cl.present[id] || cl.leaving[id])
				s_held.Push(id);
		}
		for (unsigned int k = 0; k < cl.maybeIds.Size(); ++k)
		{
			const WORD id = cl.maybeIds[k];
			if (cl.maybe[id] || cl.leaving[id])
				s_held.Push(id);
		}
		for (unsigned int f = 0; f < cl.pending.Size(); ++f)
		{
			const SnapPendingFrame & pf = cl.pending[f];
			for (unsigned int r = 0; r < pf.records.Size(); ++r)
			{
				if (pf.records[r].entered)
					s_held.Push(pf.records[r].id);
			}
		}
		snapcl_ForgetWorld(cl);
		for (unsigned int k = 0; k < s_held.Size(); ++k)
		{
			const WORD id = s_held[k];
			if (cl.maybe[id] == 0)
			{
				cl.maybe[id] = 1;
				cl.maybeIds.Push(id);
				++g_maybeMarked;
			}
		}
		++g_rebaselines;
	}

	// Compact the believed-id list once stale entries outnumber live ones.
	if (cl.staleIds > cl.believedCount)
	{
		TArray<WORD> live;
		for (unsigned int i = 0; i < cl.believedIds.Size(); ++i)
		{
			const WORD id = cl.believedIds[i];
			if (cl.present[id])
				live.Push(id);
			else
				cl.listed[id] = 0;
		}
		cl.believedIds = live;
		cl.staleIds = 0;
	}

	// Whether the client is still filling (under 3/4 of the world).
	const bool fillMode = ((QWORD)cl.believedCount * 4 < (QWORD)worldCount * 3);

	// The fill clock, from the first frame for a client holding nothing to
	// its holding the world, by acked delivery.
	if ((cl.fillStartMS == 0) && (cl.fillReported == false) && (worldCount > 0)
		&& (cl.believedCount == 0))
		cl.fillStartMS = I_MSTime();
	else if ((cl.fillStartMS != 0) && (cl.believedCount >= worldCount))
	{
		const unsigned int now = I_MSTime();
		Printf("[snap] JOIN FILL: client %u reached %u of %u entities in %u ms\n",
			client, cl.believedCount, worldCount,
			(now >= cl.fillStartMS) ? (now - cl.fillStartMS) : 0u);
		cl.fillStartMS = 0;
		cl.fillReported = true;		// once per fill, not once per frame after it
	}

	// No frame-wide row list: each record's pending row is pushed into the
	// chunk that carries it, at encode.

	// Track which ids are in the world this frame so departures can be found.
	static BYTE* pInWorld = NULL;
	if (pInWorld == NULL)
		pInWorld = new BYTE[SNAP_MAXID];
	memset(pInWorld, 0, SNAP_MAXID);

	BYTE record[SNAP_MAX_RECORD_BYTES];

	for (unsigned int i = 0; i < worldCount; ++i)
		pInWorld[world[i].id] = 1;

	// ---- departures first ----
	//
	// Leaves go before the entity loop, which would starve them, on at most
	// half the budget, so a mass extinction cannot starve movement.
	const unsigned int departureReserve = (budgetBytes / 2 > 0) ? (budgetBytes / 2) : budgetBytes;

	for (unsigned int k = 0; k < cl.believedIds.Size(); ++k)
	{
		const unsigned int id = cl.believedIds[k];
		if ((cl.present[id] == 0) || pInWorld[id])
			continue;
		if (g_prefixBytes + 3 > departureReserve)
			break;

		SnapEntityState dummy;
		memset(&dummy, 0, sizeof(dummy));
		const unsigned int n = WriteRecordBody(record, ServerMapFrame(), 0, 0, dummy);
		SnapEmitRec & er = snapcl_PushPrefix(record, n, (WORD)id, SNAPEMIT_ENTITY);
		NoteCosmeticRecord(id, n + SNAP_REC_IDBYTES_RAW);

		er.sent.id = (WORD)id;
		er.sent.left = true;
		er.sent.entered = false;
		er.sent.sentMask = 0;
		er.sent.offset = 0;		// assigned at layout
		memset(&er.sent.state, 0, sizeof(er.sent.state));

		// Re-sent every frame until it folds, so this is the newest leave.
		cl.leaving[id] = 1;
		cl.leaveFrame[id] = frame;
	}

	// The same leave for a `maybe` id once out of the world, the enter path
	// covering one still in it, cleared marks compacted out.
	for (unsigned int k = 0; k < cl.maybeIds.Size(); )
	{
		const WORD id = cl.maybeIds[k];
		if ((cl.maybe[id] == 0) || cl.present[id])
		{
			cl.maybe[id] = 0;
			cl.maybeIds[k] = cl.maybeIds[cl.maybeIds.Size() - 1];
			cl.maybeIds.Pop();
			continue;
		}
		++k;
		if (pInWorld[id])
			continue;
		if (g_prefixBytes + 3 > departureReserve)
			break;

		SnapEntityState dummy;
		memset(&dummy, 0, sizeof(dummy));
		const unsigned int n = WriteRecordBody(record, ServerMapFrame(), 0, 0, dummy);
		SnapEmitRec & er = snapcl_PushPrefix(record, n, id, SNAPEMIT_ENTITY);
		NoteCosmeticRecord(id, n + SNAP_REC_IDBYTES_RAW);

		er.sent.id = id;
		er.sent.left = true;
		er.sent.entered = false;
		er.sent.sentMask = 0;
		er.sent.offset = 0;		// assigned at layout
		memset(&er.sent.state, 0, sizeof(er.sent.state));

		if (cl.leaving[id] == 0)
			++g_maybeLeaves;
		cl.leaving[id] = 1;
		cl.leaveFrame[id] = frame;
	}

	// ---- then movers, on a reserved pass with a ceiling ----
	//
	// Ahead of the ranked drain, which would leave it unreachable, with a
	// ceiling that only stops the pass (5.2).
	{
		const unsigned int slots = MoverSlotCount();
		unsigned int emitted = 0;
		unsigned int examined = 0;
		for (unsigned int k = 0; k < slots; ++k)
		{
			if (emitted >= SNAPCL_MOVER_MAX)
				break;
			if (g_prefixBytes + SNAP_MAX_EVENT_BYTES > budgetBytes)
			{
				++g_moverOutOfBytes;
				break;
			}
			++examined;

			// Resumes where the last walk stopped, not at slot 0.
			const unsigned int slot = (cl.moverCursor + k) % slots;

			BYTE cls = 0, mflags = 0, gen = 0;
			WORD index = 0, seq = 0;
			BYTE body[SNAPMV_BODY_MAX];
			unsigned int bodyBytes = 0;
			if (MoverSlotRead(slot, cls, mflags, index, gen, seq, body,
				bodyBytes) == false)
				continue;			// free slot, or a class this build cannot write

			// The whole dirty test, the capture bumping a seq only when a
			// compared field moves.
			if (seq == cl.moverSentSeq[slot])
				continue;

			BYTE mrec[SNAP_MAX_EVENT_BYTES];
			const unsigned int n = WriteMoverBody(mrec, cls, mflags, index,
				gen, body, bodyBytes);

			// Id 0, the index riding in the body, so each costs a 1-byte gap.
			SnapEmitRec & er = snapcl_PushPrefix(mrec, n, 0, SNAPEMIT_MOVER);
			er.msent.slot = (WORD)slot;
			er.msent.seq = seq;
			er.msent.offset = 0;		// assigned at layout

			// Optimistic, as psprSent is, and reverted by repair.
			cl.moverSentSeq[slot] = seq;
			++emitted;
			++g_moverEmitted;
			if (( mflags & SNAPMVF_BLOCKED ) != 0 )
				++g_moverEmitBlocked;
			if ( cls < SNAPMV_COUNT )
				++g_moverEmitCls[cls];
			if (( mflags & SNAPMVF_STAIR ) != 0 )
				++g_moverEmitStair;
		}
		if (slots > 0)
			cl.moverCursor = (cl.moverCursor + examined) % slots;
		if (emitted >= SNAPCL_MOVER_MAX)
			++g_moverCapped;
	}

	// ---- then psprite state, before the priority drain ----
	//
	// Ahead of the ranked drain as departures are, by whole-state inequality,
	// the cap only stopping the pass (9.2).
	{
		const SnapPSpriteState * const pspr = PSpriteCapture();
		unsigned int psprEmitted = 0;
		for (unsigned int pl = 0; pl < MAXPLAYERS; ++pl)
		{
			// The receiving client predicts its own psprites and must never be
			// overwritten by them (section 4.2).
			if (pl == client)
				continue;
			if (psprEmitted >= SNAPCL_PSPRITE_MAX)
				break;
			if (g_prefixBytes + SNAP_MAX_EVENT_BYTES > budgetBytes)
				break;
			// Which state, not how far in, qtics riding as in APPEAR.
			if ((pspr[pl].weaponClass == cl.psprSent[pl].weaponClass)
				&& (pspr[pl].qstate[0] == cl.psprSent[pl].qstate[0])
				&& (pspr[pl].qstate[1] == cl.psprSent[pl].qstate[1]))
				continue;

			// Only for a client already told of the player's reserved id.
			const unsigned int rid = PlayerReservedId(pl);
			if ((rid == 0) || (rid >= SNAP_MAXID) || (cl.present[rid] == 0))
				continue;

			BYTE prec[SNAP_MAX_EVENT_BYTES];
			const unsigned int n = WritePSpriteBody(prec, pspr[pl]);
			SnapEmitRec & er = snapcl_PushPrefix(prec, n, (WORD)rid,
				SNAPEMIT_PSPRITE);

			er.psent.player = (WORD)pl;
			er.psent.offset = 0;		// assigned at layout
			er.psent.state = pspr[pl];

			cl.psprSent[pl] = pspr[pl];
			++psprEmitted;
			++g_psprEmitted;
		}
		if (psprEmitted >= SNAPCL_PSPRITE_MAX)
			++g_psprCapped;
	}

	// ---- then the local player's authoritative block ----
	//
	// Unconditional while the client has a pawn and staged before the budget
	// test, since a frame without it stalls reconciliation (5.2).
	{
		SnapLocalState local;
		const int cap = LocalCapture(client, local);
		if (cap == SNAPLOCAL_CAP_OK)
		{
			const unsigned int rid = PlayerReservedId(client);
			// The client's own reserved id, which needs no cl.present test.
			if ((rid != 0) && (rid < SNAP_MAXID))
			{
				BYTE lrec[SNAP_MAX_EVENT_BYTES];
				const unsigned int n = WriteLocalBody(lrec, ServerMapFrame(), local);
				snapcl_PushPrefix(lrec, n, (WORD)rid, SNAPEMIT_NOROW);
				++g_localEmitted;
				// Read back off the record written, not recomputed from the
				// cvar: this is the value the client will act on.
				if ((local.flags & SNAPLOCAL_AUTHORITATIVE) != 0)
					++g_localAuth;
			}
			else
			{
				++g_localNoId;
			}
		}
		else if (cap == SNAPLOCAL_CAP_SPECTATOR)
		{
			++g_localSpectator;
		}
		else if (cap == SNAPLOCAL_CAP_NOCLIENT)
		{
			++g_localNoClient;
		}
		else
		{
			++g_localNoPawn;
		}
	}

	// ---- then entity updates, in priority order ----
	//
	// Each dirty entity's band, then the bands drained, in two linear passes.
	static TArray<unsigned int> s_tier[SNAPCL_TIERS];
	static TArray<BYTE> s_mask;
	static TArray<BYTE> s_forced;
	// The change mask before unackedMask widens it, for the fresh metric.
	static TArray<BYTE> s_changed;
	unsigned int cosmeticCount = 0;
	unsigned int cosmeticPosCount = 0;
	unsigned int culledCount = 0;		// demoted by the interest cull

	for (unsigned int t = 0; t < SNAPCL_TIERS; ++t)
	{
		s_tier[t].Clear();
		g_lastTierCount[t] = 0;
		g_lastTierSize[t] = 0;
	}
	if (s_mask.Size() < worldCount)
		s_mask.Resize(worldCount);
	if (s_forced.Size() < worldCount)
		s_forced.Resize(worldCount);
	if (s_changed.Size() < worldCount)
		s_changed.Resize(worldCount);

	double viewCos = 1.0, viewSin = 0.0;
	if (view.valid)
	{
		// One trig pair per client per frame, not per entity.
		const double ang = (double)view.angle * (2.0 * 3.14159265358979 / 4294967296.0);
		viewCos = cos(ang);
		viewSin = sin(ang);
	}

	if (worldCount > 0)
		cl.scanStart %= worldCount;
	else
		cl.scanStart = 0;

	for (unsigned int step = 0; step < worldCount; ++step)
	{
		// Rotating start so that, within a band, the same entities do not
		// always sit at the front.
		const unsigned int i = (cl.scanStart + step) % worldCount;
		const SnapEntityState& cur = world[i];

		// Branches first, ahead of the `mask == 0` exit, which would drop one
		// on an entity with nothing else to send.
		if (view.valid && BranchedThisTic(cur.id))
		{
			const double bdx = (double)PosToUnits(cur.qx) - (double)(view.x >> FRACBITS);
			const double bdy = (double)PosToUnits(cur.qy) - (double)(view.y >> FRACBITS);
			const int dist = (int)sqrt(bdx * bdx + bdy * bdy);
			const int stale = (int)(WORD)((WORD)frame - cl.lastSentFrame[cur.id]);

			// Map units per tic, not fixed_t: the census buckets at 8 units.
			// [Kaori-Chani] A 48.16 shifted right by FRACBITS is still a fixed_t, holding whole
			// units in its raw, so the narrowing to int is written out. A velocity per tic is far
			// inside an int either way; what the cast rules out is the shift being forgotten and
			// a raw fixed_t landing in the census as if it were units.
			int vel = (int)(DequantVel(cur.qvx) >> FRACBITS); if (vel < 0) vel = -vel;
			int vy = (int)(DequantVel(cur.qvy) >> FRACBITS); if (vy < 0) vy = -vy;

			// One definition of terminal, the scheduler's: the corpse bit alone
			// misses every exploding missile.
			const bool bTerminal = ((cur.misc & SNAPMISC_CORPSE) != 0)
				|| ((priority != NULL) && (priority[i] & SNAPPRI_TERMINAL));
			BranchCensus(dist, stale, vel + vy, bTerminal);

			// Queued on entity state's radius for an entity the client already
			// believes in, since it could not apply one otherwise.
			if ((cl.present[cur.id] != 0) && (dist <= (int)fua_snap_cull_dist))
			{
				QueueEvent(client, (WORD)cur.id, SNAPEV_BRANCH, cur,
					frame, gameTic, bTerminal);
			}
		}

		BYTE mask;
		BYTE changed;
		bool transition = false;
		// Leaving too: back in the world before its leave folded, so the client
		// may already have dropped it (see `leaving`).
		if ((cl.present[cur.id] == 0) || cl.leaving[cur.id])
		{
			mask = SNAPGRP_ALL | SNAPGRP_ENTER;
			changed = mask;			// an enter is entirely new information
		}
		else
		{
			const SnapEntityState& was = cl.sent[cur.id];
			mask = DiffMask(was, cur);

			// Position withheld while the client's extrapolation of a missile
			// suppressible in `was` and `cur` stays accurate.
			if ((fua_snap_missile_predict > 0) && ((mask & SNAPGRP_POS) != 0)
				&& IsSuppressible(was.misc)
				&& IsSuppressible(cur.misc))
			{
				const WORD posAt = cl.lastPosFrame[cur.id];
				const unsigned int dt = (posAt == 0) ? 0xFFFF
					: (unsigned int)(WORD)((WORD)frame - posAt);

				// Never past what the client can extrapolate, or it freezes it.
				if ((dt == 0) || (dt > SNAP_BALLISTIC_MAX_TICS))
				{
					if (dt != 0)
						++g_predExpired;
				}
				else
				{
					double px, py, pz;
					ExtrapBallistic(was, (double)dt, px, py, pz);
					const double dx = px - (double)DequantPos(cur.qx);
					const double dy = py - (double)DequantPos(cur.qy);
					const double dz = pz - (double)DequantPos(cur.qz);
					const double err = sqrt(dx * dx + dy * dy + dz * dz) / 65536.0;

					if (err <= (double)(int)fua_snap_missile_predict)
					{
						mask &= (BYTE)~(SNAPGRP_POS | SNAPGRP_POSZ);
						++g_predHeld;
						if ((unsigned int)err > g_predWorst)
							g_predWorst = (unsigned int)err;
					}
					else
						++g_predSent;
				}
			}

			// The gravity class by the law from `was`, velocity within a
			// quantum, elapsed time in frames, which catch-up fails safe on.
			const BYTE lawGroups = SNAPGRP_POS | SNAPGRP_POSZ | SNAPGRP_VELMISC;
			// cur's class: a reference is built by merging groups, which never
			// carry it, so was.classId reads 0.
			double lawG = 0.0;
			bool bOldZ = false;
			if ((fua_snap_missile_predict > 0) && ((mask & lawGroups) != 0)
				&& IsGravBallistic(was.misc) && IsGravBallistic(cur.misc)
				&& LawAt(cur.classId, DequantPos(was.qx), DequantPos(was.qy), lawG, bOldZ))
			{
				const WORD posAt = cl.lastPosFrame[cur.id];
				const unsigned int dt = (posAt == 0) ? 0xFFFF
					: (unsigned int)(WORD)((WORD)frame - posAt);
				if ((dt == 0) || (dt > SNAP_BALLISTIC_MAX_TICS))
				{
					if (dt != 0)
						++g_lawExpired;
				}
				else
				{
					const double g = lawG;
					const double n = (double)dt;
					const double vz0 = (double)DequantVel(was.qvz);
					double px, py, pz;
					ExtrapLaw(was, vz0, n, g, bOldZ, px, py, pz);
					const double dx = px - (double)DequantPos(cur.qx);
					const double dy = py - (double)DequantPos(cur.qy);
					const double dz = pz - (double)DequantPos(cur.qz);
					const double err = sqrt(dx * dx + dy * dy + dz * dz) / 65536.0;
					const double dvz = (vz0 - g * n) - (double)DequantVel(cur.qvz);
					const bool bVelLaw = (fabs(dvz) <= (double)DequantVel(1))
						&& (was.qvx == cur.qvx) && (was.qvy == cur.qvy)
						&& (was.misc == cur.misc) && (was.effects == cur.effects);
					if (err > (double)(int)fua_snap_missile_predict)
						++g_lawSentPos;
					else if (bVelLaw == false)
						++g_lawSentVel;
					else
					{
						mask &= (BYTE)~lawGroups;
						++g_lawHeld;
						if ((unsigned int)err > g_lawWorst)
							g_lawWorst = (unsigned int)err;
					}
				}
			}

			// Appearance suppression: APPEAR withheld while the walk from what
			// the client holds reaches the server's state.
			if (fua_snap_appear_predict && ((mask & SNAPGRP_APPEAR) != 0)
				&& !(fua_snap_appear_exempt && AppearExempt((WORD)cur.id)))
			{
				const WORD apAt = cl.lastAppearFrame[cur.id];
				const int dt = (apAt == 0) ? 0
					: (int)(unsigned int)(WORD)((WORD)frame - apAt);
				const int r = AppearDerivable(cur.classId, was.qstate,
					was.qtics, dt, cur.qstate);
				const unsigned int apop = snapcl_AppearPop(cur.id);
				if (r == SNAP_APPEAR_DERIVABLE)
				{
					mask &= (BYTE)~SNAPGRP_APPEAR;
					++g_apHeld[apop];
				}
				else if (r == SNAP_APPEAR_DIVERGED)
					++g_apDiverged[apop];
				else
					++g_apUnwalkable[apop];
			}

			// The corpse bit newly set: a death, the one branch promoted.
			if ((cur.misc & 1) && ((was.misc & 1) == 0))
				transition = true;

			changed = mask;

			// Re-send any group left in doubt by an unacknowledged frame, even
			// if it currently matches what the client acknowledged.
			mask |= cl.unackedMask[cur.id];

			// The gravity class's position and velocity travel together, so
			// lastPosFrame is both groups' reference tic.
			if (IsGravBallistic(cur.misc) && (mask & lawGroups))
				mask |= lawGroups;

			// ...but not every frame, or unacked re-sends would spin on an idle
			// budget (lastSentFrame not stamped, which this reads).
			if (((changed & SNAPGRP_GROUPMASK) == 0) && (mask != 0))
			{
				const WORD sentAt = cl.lastSentFrame[cur.id];
				// Never sent: not a re-send, let it through.
				if (sentAt != 0)
				{
					const unsigned int sinceSent =
						(unsigned int)(WORD)((WORD)frame - sentAt);
					// One frame of margin over the pending depth, so a client
					// whose acks are merely slow is not treated as lossy.
					const unsigned int ackWindow = cl.pending.Size() + 1;
					if (sinceSent <= ackWindow)
						continue;
				}
			}

			if (mask == 0)
			{
				// Nothing outstanding, so its age, frames spent wanting to be
				// sent and losing, is stamped.
				cl.lastSentFrame[cur.id] = (WORD)frame;
				continue;			// unchanged, and nothing in doubt
			}
		}

		s_mask[i] = mask;
		s_changed[i] = changed;

		static const BYTE owedBits[SNAP_GROUP_COUNT] =
		{
			SNAPGRP_POS, SNAPGRP_POSZ, SNAPGRP_ANG, SNAPGRP_VELMISC,
			SNAPGRP_APPEAR, SNAPGRP_RENDER
		};
		++g_owedTotal;
		for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
			if (mask & owedBits[g])
				++g_owedByGroup[g];

		// Frames since this entity was last emitted, in 1, 3 and 10 s buckets.
		const WORD emitAt = cl.lastSentFrame[cur.id];
		const unsigned int since = (emitAt == 0) ? 0xFFFF : (unsigned int)((WORD)((WORD)frame - emitAt));
		const int sb = (since < 35) ? 0 : (since < 105) ? 1 : (since < 350) ? 2 : 3;
		++g_owedStale[sb];
		if (mask & (SNAPGRP_POS | SNAPGRP_POSZ))
			++g_owedPosStale[sb];

		const WORD sentAt = cl.lastSentFrame[cur.id];
		const unsigned int age = (sentAt == 0) ? SNAPCL_AGE_FORCE
			: (unsigned int)(WORD)((WORD)frame - sentAt);

		// Never actually refreshed reads as overdue for refresh, which is the
		// safe direction: it costs one record, not a permanently frozen ghost.
		const WORD refreshAt = cl.lastRefreshFrame[cur.id];
		const unsigned int refreshAge = (refreshAt == 0) ? SNAPCL_AGE_COSMETIC
			: (unsigned int)(WORD)((WORD)frame - refreshAt);

		const BYTE pri = (priority != NULL) ? priority[i] : 0;
		bool forced = false, cosmetic = false, culled = false;
		unsigned int distBucket = 0;
		bool inFront = true;
		unsigned int tier = snapcl_Tier(cur, pri, mask,
			view, viewCos, viewSin, age, refreshAge, forced, cosmetic, culled,
			distBucket, inFront, fillMode, transition);

		if (tier < SNAPCL_TIERS_REPORT)
		{
			++g_bandRecs[tier];
			if (mask & (SNAPGRP_POS | SNAPGRP_POSZ))
				++g_bandPos[tier];
		}

		// Re-entry, an edge from held (the last three bands, or culled) to
		// serviceable, read from the resulting band.
		{
			const BYTE heldNow = (culled || (tier >= SNAPCL_TIER_REFRESH)) ? 1 : 0;
			const BYTE heldBefore = cl.wasHeld[cur.id];
			cl.wasHeld[cur.id] = heldNow;

			if (heldBefore && (heldNow == 0))
			{
				++g_reentrySeen;
				const unsigned int a = refreshAge;
				const int rb = (a < 35) ? 0 : (a < 105) ? 1 : (a < 210) ? 2 : (a < 420) ? 3 : 4;
				++g_reentryHist[rb];
			}
		}

		// The contested set by ring and in front, withheld ones left out.
		if (view.valid && (cosmetic == false))
		{
			if (distBucket < (unsigned int)SNAPCL_DIST_BUCKETS)
			{
				g_censusByDist[distBucket]++;
				if (inFront)
					g_censusFrontByDist[distBucket]++;
			}
			g_censusContested++;
			if (inFront)
				g_censusFront++;
		}
		s_forced[i] = forced ? 1 : 0;

		// The quota's denominator: held entities owing position.
		const bool owesPos = ((mask & (SNAPGRP_POS | SNAPGRP_POSZ)) != 0);
		if (owesPos && (cosmetic || (culled && (cosmetic == false))))
			++cosmeticPosCount;

		if (cosmetic)
		{
			++cosmeticCount;

			// A cosmetic record was declined, not refused, so it earns no
			// starvation credit.
			cl.lastSentFrame[cur.id] = (WORD)frame;
		}

		// The same for the cull, counted once into `withheld`.
		if (culled && (cosmetic == false))
		{
			++culledCount;
			cl.lastSentFrame[cur.id] = (WORD)frame;
		}
		s_tier[tier].Push(i);
	}

	// Pass 2: drain bands in order.
	unsigned int emitted = 0, considered = 0, forcedEmitted = 0;

	// Records telling the client something new, not a re-send awaiting an ack.
	unsigned int freshEmitted = 0;
	unsigned int resendEmitted = 0;		// records carrying nothing new
	unsigned int resendBytes = 0;		// group bytes present only for the ack
	bool budgetHit = false;

	// The refresh bands drain first on quotas sized to cycle their population
	// in their interval, clamped, past which the interval stretches.
	unsigned int refreshQuota = (cl.lastCosmeticCount / SNAPCL_AGE_COSMETIC) + 1;
	if ((fua_snap_refresh_max > 0) && (refreshQuota > (unsigned int)fua_snap_refresh_max))
	{
		// Clamped, so the interval is not met, which is reported.
		if (g_lastRefreshWanted == 0)
			Printf("[snap] refresh clamped: wanted %u/frame, capped at %d - the"
				" cosmetic interval is now ~%.1f s, not %d s\n",
				refreshQuota, (int)fua_snap_refresh_max,
				(double)SNAPCL_AGE_COSMETIC * (double)refreshQuota
				/ (double)(int)fua_snap_refresh_max / (double)TICRATE,
				SNAPCL_AGE_COSMETIC / TICRATE);
		g_lastRefreshWanted = refreshQuota;
		refreshQuota = (unsigned int)fua_snap_refresh_max;
	}
	else
		g_lastRefreshWanted = 0;

	// The same for appearance, capped at SNAPCL_ANIMREF_MAX (5.1).
	unsigned int animrefQuota = (cl.lastAnimrefCount / (unsigned int)(int)fua_snap_animref_interval) + 1;
	if (animrefQuota > (unsigned int)SNAPCL_ANIMREF_MAX)
	{
		if (g_lastAnimrefWanted == 0)
			Printf("[snap] animref clamped: wanted %u/frame, capped at %d - the"
				" appearance interval is now ~%.1f s, not %.1f s\n",
				animrefQuota, (int)SNAPCL_ANIMREF_MAX,
				(double)(int)fua_snap_animref_interval * (double)animrefQuota
				/ (double)SNAPCL_ANIMREF_MAX / (double)TICRATE,
				(double)(int)fua_snap_animref_interval / (double)TICRATE);
		g_lastAnimrefWanted = animrefQuota;
		animrefQuota = (unsigned int)SNAPCL_ANIMREF_MAX;
	}
	else
		g_lastAnimrefWanted = 0;

	// The cvar may clamp this band, never close it: at zero the far tier freezes
	// and appearance suppression loses its only unbiased sample.
	if (animrefQuota == 0)
		animrefQuota = 1;
	g_lastAnimrefQuota = animrefQuota;

	const unsigned int refreshCeiling = g_prefixBytes + (budgetBytes / SNAPCL_REFRESH_CAP_DIV);
	unsigned int refreshEmitted = 0;
	unsigned int animrefEmitted = 0;

	// The forced and absence bands take at most half the budget.
	const unsigned int agingCap = g_prefixBytes + (budgetBytes / 2);

	// The reserve for the bands below d0, each held to a slice by population
	// and then spent again unrestricted (9.2; 0 off).
	unsigned int reservePop = 0;
	for (unsigned int t = SNAPCL_TIER_DIST + 1; t <= SNAPCL_TIER_DISTLAST; ++t)
		reservePop += s_tier[t].Size();

	unsigned int reservedBytes = 0;
	unsigned int reservedBase = 0;
	g_lastReserveAllowed = g_lastReserveSpent = 0;
	g_lastReserveCapped = g_lastReserveDrained = 0;
	if ((fua_snap_bandreserve > 0) && (reservePop > 0))
	{
		unsigned int pct = (unsigned int)fua_snap_bandreserve;
		if (pct > 100)
			pct = 100;
		reservedBytes = (budgetBytes * pct) / 100;
	}

	// Events first, within their share, so a brief transition keeps its frame.
	snapcl_WriteEvents(cl, budgetBytes, frame);

	// The cursor starts at what the staged prefix will occupy.
	unsigned int emitBytes = g_prefixBytes;

	for (unsigned int pass = 0; (pass < 4) && (budgetHit == false); ++pass)
	{
		// The four passes of sv_snapclient.md's step 7, the reserve after
		// pass 1, whose aging cap it would otherwise spend through.
		if ((pass == 2) && (reservedBytes == 0))
			continue;

		// Taken at the start of the reserved pass, so its ceilings measure from
		// what the earlier passes already spent.
		unsigned int cumReservePop = 0;
		if (pass == 2)
		{
			reservedBase = emitBytes;
			g_lastReserveAllowed = reservedBytes;
		}

		unsigned int tFirst, tLast;
		switch (pass)
		{
		case 0:  tFirst = SNAPCL_TIER_REFRESH; tLast = SNAPCL_TIER_ANIMREFRESH; break;
		case 1:  tFirst = SNAPCL_TIER_OVERDUE; tLast = SNAPCL_TIER_MISSILE;  break;
		case 2:  tFirst = SNAPCL_TIER_DIST + 1; tLast = SNAPCL_TIER_DISTLAST; break;
		default: tFirst = SNAPCL_TIER_OVERDUE; tLast = SNAPCL_TIER_COSMETIC; break;
		}

		for (unsigned int t = tFirst; (t <= tLast) && (budgetHit == false); ++t)
		{
			if ((pass != 0) && ((t == SNAPCL_TIER_REFRESH) || (t == SNAPCL_TIER_ANIMREFRESH)))
				continue;			// already drained above

			// The missile band's ceiling, against a burst taking the frame.
			unsigned int tierCeiling = 0;
			if ((t == SNAPCL_TIER_MISSILE) && (fua_snap_missile_share > 0))
			{
				unsigned int pctM = (unsigned int)fua_snap_missile_share;
				if (pctM > 100)
					pctM = 100;
				tierCeiling = emitBytes + (budgetBytes * pctM) / 100;
			}

			// The reserved pass covers the DISTANCE bands only; its range
			// already excludes everything else, so no skip is needed here.

			// The absence bands, ENTER too, capped so they cannot starve d0.
			unsigned int cap = (pass == 0) ? refreshCeiling : budgetBytes;
			if ((tierCeiling > 0) && (tierCeiling < cap))
				cap = tierCeiling;		// missile band ceiling, see above
			if ((pass != 0) && (t <= SNAPCL_TIER_ENTER) && (agingCap < cap))
				cap = agingCap;

			// The reserved pass's cumulative ceiling, growing by each band's
			// slice, so a band's surplus passes to the next.
			if (pass == 2)
			{
				cumReservePop += s_tier[t].Size();
				const unsigned int ceiling = reservedBase
					+ (unsigned int)(((QWORD)reservedBytes * cumReservePop) / reservePop);
				if (ceiling < cap)
					cap = ceiling;
			}

			bool bandStoppedOnCap = false;

			// Each band is walked from its persistent cursor, so late spawns
			// are not served last.
			const unsigned int bandSize = s_tier[t].Size();
			const unsigned int startAt = (bandSize > 0)
				? (cl.bandCursor[t] % bandSize) : 0;
			unsigned int walked = 0;

			for (unsigned int kk = 0; kk < bandSize; ++kk)
			{
				const unsigned int k = (startAt + kk) % bandSize;
				const unsigned int i = s_tier[t][k];
				++walked;
				const SnapEntityState& cur = world[i];
				BYTE mask = s_mask[i];

				// Served already, s_mask being cleared on emit.
				if (mask == 0)
					continue;

				++considered;

				// A quota per refresh band in pass 0.
				if (pass == 0)
				{
					if ((t == SNAPCL_TIER_REFRESH) && (refreshEmitted >= refreshQuota))
						break;
					if ((t == SNAPCL_TIER_ANIMREFRESH) && (animrefEmitted >= animrefQuota))
						break;
				}

				if (emitBytes + RecordBytesMax(ServerMapFrame()) > cap)
				{
					// Each cap means what sv_snapclient.md's rule says, the
					// budget alone stopping everything.
					const bool agingCapped = (pass != 0) && (t <= SNAPCL_TIER_ENTER)
						&& (cap == agingCap) && (agingCap < budgetBytes);
					const bool sliceCapped = (pass == 2) && (cap < budgetBytes);

					// The missile ceiling stops that band only.
					const bool missileCapped = (t == SNAPCL_TIER_MISSILE)
						&& (tierCeiling > 0) && (cap == tierCeiling);
					if (sliceCapped)
					{
						++g_lastReserveCapped;	// band stopped by its own ceiling
						bandStoppedOnCap = true;
					}
					if ((pass != 0) && (agingCapped == false) && (sliceCapped == false)
						&& (missileCapped == false))
						budgetHit = true;
					break;			// next band (or done, if budgetHit)
				}

				if ((priority != NULL) && (priority[i] & SNAPPRI_TERMINAL))
					++g_terminalEmitted;

				const WORD classId = (mask & SNAPGRP_ENTER) ? cur.classId : 0;
				const WORD targetId = (mask & SNAPGRP_ENTER) ? EnterTarget(cur.id) : 0;
				if (targetId != 0)
					++g_targetEnters;

				// The wait of this client's own missile from spawn to its enter
				// leaving, measured at the emit.
				if (mask & SNAPGRP_ENTER)
				{
					int spawnTic = 0;
					if (OwnMissileFirstEnter(cur.id, client, spawnTic))
					{
						const int wait = (int)gameTic - spawnTic;
						++g_ownEnterN;
						if (wait > (int)g_ownEnterWorst)
							g_ownEnterWorst = (unsigned int)wait;
						++g_ownEnterHist[ComputeOwnEnterBucket(wait)];
					}
				}

				// What the client will hold once this lands, which the checksum
				// is stamped from, so a withheld group is no desync.
				const BYTE sentMask = (BYTE)(mask & SNAPGRP_GROUPMASK);
				SnapEntityState willHold = cl.sent[cur.id];
				MergeGroups(willHold, ServerMapFrame(), cur, sentMask);

				// Encoded now, laid out later: the payload offset is not known
				// until emission order is fixed, so it is patched at layout.
				SnapEmitRec er;
				const unsigned int n = WriteRecordBody(er.bytes, ServerMapFrame(),
					mask, classId, cur, &willHold, targetId);
				er.id = cur.id;
				er.kind = SNAPEMIT_ENTITY;
				er.len = (BYTE)n;
				er.census = true;
				er.censusTier = t;
				er.censusMask = mask;
				er.censusState = cur;
				er.sent.id = cur.id;
				er.sent.left = false;
				er.sent.entered = ((mask & SNAPGRP_ENTER) != 0);
				er.sent.sentMask = sentMask;
				if (er.sent.entered && cl.present[cur.id])
					++g_reentersInFlight;
				er.sent.offset = 0;		// assigned at layout
				er.sent.state = cur;
				g_emitRecs.Push(er);
				emitBytes += n + SNAP_REC_IDBYTES_RAW;	// see g_prefixBytes
				NoteCosmeticRecord(cur.id, n + SNAP_REC_IDBYTES_RAW);
				s_mask[i] = 0;			// served; do not revisit in the mop-up pass
				cl.unackedMask[cur.id] |= sentMask;

				// The same merge the checksum was stamped from, assigned.
				cl.sent[cur.id] = willHold;

				// Fresh unless its change mask before widening was empty.
				const BYTE chg = s_changed[i];
				if ((chg & SNAPGRP_GROUPMASK) != 0)
					++freshEmitted;
				else
					++resendEmitted;

				// The bytes of groups present only because unacked, sized from
				// the state written.
				{
					const BYTE widened = (BYTE)(mask & SNAPGRP_GROUPMASK
						& ~(chg & SNAPGRP_GROUPMASK));
					if (widened != 0)
						resendBytes += GroupBytesFor(ServerMapFrame(), widened, cur);
				}

				cl.lastSentFrame[cur.id] = (WORD)frame;

				// Only a real emit clears the refresh clock; this is the one place
				// the client has actually been told something.
				cl.lastRefreshFrame[cur.id] = (WORD)frame;

				// Stamped from sentMask, so a withheld group cannot claim to have
				// travelled.
				if (sentMask & SNAPGRP_POS)
					cl.lastPosFrame[cur.id] = (WORD)frame;
				if (sentMask & SNAPGRP_APPEAR)
					cl.lastAppearFrame[cur.id] = (WORD)frame;

				++emitted;
				if (pass == 0)
				{
					if (t == SNAPCL_TIER_ANIMREFRESH)
						++animrefEmitted;
					else
						++refreshEmitted;
				}
				++g_lastTierCount[t];
				if (s_forced[i])
					++forcedEmitted;
			}

			// Resumes next frame past what was walked, skips included.
			if (bandSize > 0)
				cl.bandCursor[t] = (startAt + walked) % bandSize;

			// A band walked through without a cap was drained, not capped.
			if ((pass == 2) && (budgetHit == false) && (bandStoppedOnCap == false))
				++g_lastReserveDrained;
		}

		if (pass == 2)
			g_lastReserveSpent = emitBytes - reservedBase;
	}

	// Count everything the budget could not carry, for diagnostics.
	unsigned int dirtyTotal = 0;
	for (unsigned int t = 0; t < SNAPCL_TIERS; ++t)
	{
		g_lastTierSize[t] = s_tier[t].Size();
		dirtyTotal += s_tier[t].Size();
	}
	g_lastDeferred = dirtyTotal - emitted;
	g_lastForced = forcedEmitted;
	g_lastFresh = freshEmitted;
	g_lastResend = resendEmitted;
	g_lastResendBytes = resendBytes;

	// The whole withheld set, or the quota would shrink as its backlog grew.
	const unsigned int withheld = cosmeticCount + culledCount
		+ s_tier[SNAPCL_TIER_REFRESH].Size();
	g_lastCosmetic = withheld;

	// Sized by the holds owing position, whose staleness it bounds.
	cl.lastCosmeticCount = cosmeticPosCount +s_tier[SNAPCL_TIER_REFRESH].Size();

	cl.lastAnimrefCount = s_tier[SNAPCL_TIER_ANIMREFRESH].Size();

	// ---- layout: sort, estimate, cut, encode ----
	//
	// Emission order is decided here alone, the entity records sorted by id
	// (insertion sort over a partly ordered few hundred).
	for (unsigned int i = 1; i < g_emitRecs.Size(); ++i)
	{
		const SnapEmitRec v = g_emitRecs[i];
		unsigned int j = i;
		while ((j > 0) && (g_emitRecs[j - 1].id > v.id))
		{
			g_emitRecs[j] = g_emitRecs[j - 1];
			--j;
		}
		g_emitRecs[j] = v;
	}

	// Emission order in one list of pointers, the prefix then the records.
	s_emit.Clear();
	for (unsigned int e = 0; e < g_prefixRecs.Size(); ++e)
		s_emit.Push(&g_prefixRecs[e]);
	for (unsigned int e = 0; e < g_emitRecs.Size(); ++e)
		s_emit.Push(&g_emitRecs[e]);

	// Each record's estimated start, every id a gap, which the cut at
	// chunkBytes - 1 bounds (sv_snapshot.md).
	unsigned int estBytes = 0;
	{
		SnapIdCursor est;
		IdCursorReset(est);
		for (unsigned int e = 0; e < s_emit.Size(); ++e)
		{
			s_bounds.Push(estBytes);
			estBytes += RecordIdBytes(s_emit[e]->id, est) + s_emit[e]->len;
			est.prev = s_emit[e]->id;
			est.first = false;
		}
	}

	// Resume the rotation past what we looked at, so a budgeted scan cannot
	// keep examining the same slice.
	if (worldCount > 0)
		cl.scanStart = (cl.scanStart + (considered ? considered : 1)) % worldCount;

	// A frame with no bytes is never recorded as pending, as nothing acks it.
	if (estBytes == 0)
		return 0;

	// ---- split into datagrams ----
	//
	// Cuts on records, each chunk's pending entry holding its own records, and
	// sequences consumed only here.
	s_bounds.Push(estBytes);		// terminator: the last record ends here

	// The cut (ComputeChunkCuts): one pass over the boundaries, by record.
	s_cuts.Resize(s_emit.Size());
	s_cuts.Resize(ComputeChunkCuts(&s_bounds[0], s_bounds.Size(), chunkBytes, &s_cuts[0]));

	// ---- encode: lay the records into the payload, chunk by chunk ----
	//
	// Each pending row goes to the chunk being written, whose range is taken
	// from the bytes written.
	for (unsigned int c = 0; c < s_cuts.Size(); ++c)
	{
		const unsigned int firstRec = s_cuts[c];
		const unsigned int lastRec =
			(c + 1 < s_cuts.Size()) ? s_cuts[c + 1] : s_emit.Size();

		SnapPendingFrame pf;
		pf.seq = cl.nextSeq++;
		pf.frame = frame;
		pf.sawUnknown = false;
		pf.lost = false;
		pf.lostUnproven = false;

		SnapChunk ch;
		ch.offset = out.Size();
		ch.seq = pf.seq;

		// The id base restarts here alone, so a chunk decodes on its own.
		SnapIdCursor ids;
		IdCursorReset(ids);

		for (unsigned int e = firstRec; e < lastRec; ++e)
		{
			SnapEmitRec & er = *s_emit[e];
			const unsigned int recOffset = out.Size();
			BYTE idbuf[SNAP_IDGAP_MAXBYTES];
			const unsigned int idBytes = WriteRecordId(idbuf, er.id, ids);
			for (unsigned int b = 0; b < idBytes; ++b)
				out.Push(idbuf[b]);
			for (unsigned int b = 0; b < er.len; ++b)
				out.Push(er.bytes[b]);
			if (er.kind == SNAPEMIT_ENTITY)
			{
				er.sent.offset = recOffset;
				pf.records.Push(er.sent);
			}
			else if (er.kind == SNAPEMIT_PSPRITE)
			{
				er.psent.offset = recOffset;
				pf.psprRecords.Push(er.psent);
			}
			else if (er.kind == SNAPEMIT_MOVER)
			{
				er.msent.offset = recOffset;
				pf.moverRecords.Push(er.msent);
			}
			if (fua_snap_wirecensus && er.census)
				snapcl_CensusRecord(er.censusTier, er.id, er.censusMask,
					idBytes + er.len, idBytes, recOffset, er.censusState);
		}

		ch.length = out.Size() - ch.offset;
		chunks.Push(ch);
		cl.pending.Push(pf);
	}

	// One client frame's emitted ids are complete, which the sorted-gap census
	// needs.
	if (fua_snap_wirecensus)
		snapcl_CensusFrameEnd(chunks);

	return out.Size();
}

//*****************************************************************************
//	DIAGNOSTICS

unsigned int zx::netsnapshot::PendingFrames(unsigned int client)
{
	return (client < MAXPLAYERS) ? g_clients[client].pending.Size() : 0;
}

unsigned int zx::netsnapshot::BelievedCount(unsigned int client)
{
	return (client < MAXPLAYERS) ? g_clients[client].believedCount : 0;
}

unsigned int zx::netsnapshot::LastAckedSeq(unsigned int client)
{
	return (client < MAXPLAYERS) ? g_clients[client].lastAckedSeq : 0;
}

// Every active client's health, the max pending depth being the signal.
void zx::netsnapshot::AggregateHealth(SnapClientHealth& out)
{
	out.activeClients = 0;
	out.believedAvg = 0;
	out.believedMax = 0;
	out.pendingAvg = 0;
	out.pendingMax = 0;
	out.rebaselines = g_rebaselines;

	QWORD believedSum = 0, pendingSum = 0;
	for (unsigned int i = 0; i < MAXPLAYERS; ++i)
	{
		const SnapClientState& cl = g_clients[i];
		if ((cl.believed == NULL) || (cl.active == false))
			continue;

		++out.activeClients;
		const unsigned int bel = cl.believedCount;
		const unsigned int pend = cl.pending.Size();
		believedSum += bel;
		pendingSum += pend;
		if (bel > out.believedMax)
			out.believedMax = bel;
		if (pend > out.pendingMax)
			out.pendingMax = pend;
	}

	if (out.activeClients > 0)
	{
		out.believedAvg = (unsigned int)(believedSum / out.activeClients);
		out.pendingAvg = (double)(QWORD)pendingSum / (double)out.activeClients;
	}
}

// The window's census totals over every BuildDelta call, which the caller
// divides by frames.
void zx::netsnapshot::GetCensus(SnapVisibilityCensus& out)
{
	out.buckets = SNAPCL_DIST_BUCKETS;
	out.contested = g_censusContested;
	out.front = g_censusFront;
	for (int i = 0; i < SNAPCL_DIST_BUCKETS; ++i)
	{
		out.byDist[i] = g_censusByDist[i];
		out.frontByDist[i] = g_censusFrontByDist[i];
	}
}

void zx::netsnapshot::ResetWindowStats()
{
	g_rebaselines = 0;
	g_censusContested = 0;
	g_censusFront = 0;
	for (int i = 0; i < SNAPCL_DIST_BUCKETS; ++i)
	{
		g_censusByDist[i] = 0;
		g_censusFrontByDist[i] = 0;
	}
}
