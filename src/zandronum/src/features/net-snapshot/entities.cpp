//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: entities.cpp
//
// Description: The client entity layer: the snapshot decoder, the ghost
// table and its sampler, the censuses behind the client report, the server
// clock model and the layer's self-tests. See entities.h.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <zstd.h>

#include "doomdef.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "c_console.h"
#include "features/net-snapshot/format.h"
#include "features/net-snapshot/computation/clock_compute.h"
#include "features/net-snapshot/computation/join_compute.h"
#include "features/net-snapshot/entities.h"
#include "features/net-snapshot/record.h"
#include "features/net-snapshot/selftest.h"
#include "networkshared.h"	// BYTESTREAM_s, for the point sound's position

using namespace zx::netsnapshot;

//*****************************************************************************
//	GHOST TABLE

// Compile-time invariant: the ghost table covers every id the wire format
// can express, so an untrusted id can never index past it.
typedef char snap_assert_id_space[(SNAP_MAXID >= 0x10000) ? 1 : -1];

// The data groups that carry a frame stamp, one stamp each.
#define CLENT_GROUPS 6
static const BYTE g_stampGroups[CLENT_GROUPS] = {
	SNAPGRP_POS, SNAPGRP_ANG, SNAPGRP_POSZ,
	SNAPGRP_APPEAR, SNAPGRP_VELMISC, SNAPGRP_RENDER };
typedef char clent_assert_stamp_groups[((SNAPGRP_POS | SNAPGRP_ANG | SNAPGRP_POSZ
	| SNAPGRP_APPEAR | SNAPGRP_VELMISC | SNAPGRP_RENDER) == SNAPGRP_ALL) ? 1 : -1];

struct SnapGhost
{
	bool			present;

	// Z acceleration in units per tic per tic, measured from two velocity
	// samples, the law's check and its fallback without the engine-side lookup.
	double			accelZ;
	// The measurement awaiting a second, agreeing reading before accelZ
	// adopts it.
	double			accelPending;
	bool			haveAccel;
	SWORD			velZSample;		// quantised velz when it was last applied
	int				velZTic;		// its GAME tic: the law runs from it
	bool			haveVelZ;
	SnapGhostSample	hist[CLENT_GHOST_HISTORY];
	unsigned int	head;		// index of the newest sample
	unsigned int	count;		// valid samples, up to CLENT_GHOST_HISTORY

	// Bumped by each event applied, which the mirror resyncs on, since an
	// event changes no sprite frame.
	BYTE			eventSeq;
	// The last event applied, its observation tic and state, against copies.
	int				lastEventTic;
	WORD			lastEventState;
	bool			haveEvent;

	// The last pose rendered and its position clock, for the jumps.
	fixed_t			lastRX, lastRY, lastRZ;
	int				lastRPosTic;
	unsigned int	lastRPosMS;
	unsigned int	recsSinceRender;	// applied to this ghost since that render
	bool			haveLastR;

	// Retained aliases for the debug accessor and existing bookkeeping.
	SnapEntityState	prevState, nextState;
	int				prevTic, nextTic;
	unsigned int	prevMS, nextMS;

	// Arrival time of this ghost's FIRST sample, so it is not drawn before
	// the delayed render clock reaches the moment it came into existence.
	unsigned int	enterMS;

	// Whether this ghost ever carried a missile bit, which a detonation
	// clears before the leave.
	bool			everMissile;

	// The shooter NetID from the enter, never a pointer, and whether the
	// mirror resolved it.
	WORD			targetNetID;
	bool			targetResolved;
	bool			targetChanged;	// one-shot: the mirror drops its old target

	// The frame that last wrote each group, which an older record leaves
	// alone; a leave sets all six.
	int				groupFrame[CLENT_GROUPS];
	// Whether any record has stamped the groups, a leave for an id not held
	// included, and whether the newest stamp is such a leave's.
	bool			stamped;
	bool			leftUnheld;
};

static SnapGhost* g_ghosts = NULL;
static unsigned int	g_presentCount = 0;

// Signed difference, so a wrapping frame counter still compares.
static inline bool clent_FrameAtOrAfter(int frame, int stamp)
{
	return (int)((unsigned int)frame - (unsigned int)stamp) >= 0;
}

// The groups in `mask` whose stamp is after `frame`.
static BYTE clent_LateGroups(const SnapGhost& g, BYTE mask, int frame)
{
	BYTE late = 0;
	for (unsigned int i = 0; i < CLENT_GROUPS; ++i)
		if (((mask & g_stampGroups[i]) != 0) && !clent_FrameAtOrAfter(frame, g.groupFrame[i]))
			late |= g_stampGroups[i];
	return late;
}

static void clent_StampGroups(SnapGhost& g, BYTE mask, int frame)
{
	g.stamped = true;
	g.leftUnheld = false;
	for (unsigned int i = 0; i < CLENT_GROUPS; ++i)
		if ((mask & g_stampGroups[i]) != 0)
			g.groupFrame[i] = frame;
}

static unsigned int clent_GroupCount(BYTE mask)
{
	unsigned int n = 0;
	for (unsigned int i = 0; i < CLENT_GROUPS; ++i)
		if ((mask & g_stampGroups[i]) != 0)
			++n;
	return n;
}

// The newest server tic in any applied record, the client's tic clock, which
// ages in exact tics.
static int			g_newestServerTic = 0;
// The newest game tic beside the newest frame, for elapsed world time.
static int			g_newestGameTic = 0;
static bool			g_haveServerTic = false;
// Bumped with every g_haveServerTic reset: a new stream, whose frame numbers
// owe nothing to the last one's.
static unsigned int	g_streamEpoch = 0;

// Ids that entered on the latest ApplyDelta.
static unsigned int* g_enteredIds = NULL;
// [Kaori-Chani] And every id a record of it was applied to, an update as much as an
// enter, since a mirror's smoothing folds the same either way.
static unsigned int* g_touchedIds = NULL;
static unsigned int  g_touchedCount = 0;
// Each id's last leave and the newest packet time on the arrival clock, the
// stamp cleared on enter.
static unsigned int* g_leftAtMS = NULL;
static unsigned int g_lastApplyMS = 0;
static unsigned int  g_enteredCount = 0;

// The fire-to-draw census's match radius, the wire carrying no owner, so the
// local player's missile is the one entering near the camera.
#define CLENT_FIRE_MATCH_UNITS   96
#define CLENT_FIRE_MATCH_MS      1000    // past this, no shot explains it

static unsigned int  g_fireMS = 0;       // when the trigger was acted on
static int           g_fireX = 0, g_fireY = 0;   // map units
static bool          g_fireArmed = false;
static unsigned int  g_fireHist[CLENT_FIRE_BUCKETS] = { 0, 0, 0, 0, 0 };
static unsigned int  g_fireSamples = 0, g_fireWorstMS = 0;
static unsigned int  g_fireUnmatched = 0;   // shots no enter was paired with
static unsigned int  g_fireTrackId = 0;     // the matched shot, still being watched
static unsigned int  g_fireTrackRecs = 0;   // POS records it has received
// Kept per ordinal record, in units a tic, not summed.
static double        g_fireJumpAcc[4] = { 0.0, 0.0, 0.0, 0.0 };
static unsigned int  g_fireJumpN[4] = { 0, 0, 0, 0 };
static double        g_fireJumpWorst[4] = { 0.0, 0.0, 0.0, 0.0 };

void zx::netsnapshot::LocalFireStats(unsigned int* hist, unsigned int& samples,
	unsigned int& worstMS, unsigned int& unmatched,
	double* jumpAcc, unsigned int* jumpN, double* jumpWorst)
{
	for (unsigned int i = 0; i < CLENT_FIRE_BUCKETS; ++i)
	{
		hist[i] = g_fireHist[i];
		g_fireHist[i] = 0;
	}
	for (unsigned int i = 0; i < 4; ++i)
	{
		jumpAcc[i] = g_fireJumpAcc[i];
		jumpN[i] = g_fireJumpN[i];
		jumpWorst[i] = g_fireJumpWorst[i];
		g_fireJumpAcc[i] = g_fireJumpWorst[i] = 0.0;
		g_fireJumpN[i] = 0;
	}
	samples = g_fireSamples;
	worstMS = g_fireWorstMS;
	unmatched = g_fireUnmatched;
	g_fireSamples = g_fireWorstMS = g_fireUnmatched = 0;
}

void zx::netsnapshot::NoteLocalFire(int x, int y, unsigned int nowMS)
{
	// A shot still waiting for its enter is replaced, not queued.
	if (g_fireArmed)
		++g_fireUnmatched;

	g_fireMS = nowMS;
	g_fireX = x;
	g_fireY = y;
	g_fireArmed = true;
}

// Sampler exits, as SnapSampleCensus in the header describes them.
static unsigned int g_sampHeld = 0, g_sampHeldNew = 0;
static unsigned int g_sampHeldMoving = 0;

static inline unsigned int clent_Population(unsigned int id);

// The heldMoving split (HeldMoveCensus), keyed on class id by open
// addressing; a class past the table's capacity counts as overflow.
#define CLENT_HM_CLASSES	512
static SnapHeldMoveClass g_hmClass[CLENT_HM_CLASSES];
static bool g_hmUsed[CLENT_HM_CLASSES];
static unsigned int g_hmOverflow[CLENT_HM_COUNT];
static unsigned int g_hmPlayers = 0;

static void clent_NoteHeldMove(const SnapGhost& g, unsigned int id)
{
	if (clent_Population(id) != CLENT_POP_OTHER)
	{
		++g_hmPlayers;
		return;
	}
	const SnapEntityState& s = g.hist[g.head].state;
	int bucket = CLENT_HM_POSONLY;
	if ((g_newestGameTic - g.hist[g.head].posTic) > CLENT_HM_REST_TICS)
		bucket = CLENT_HM_RESTED;
	else if (IsSteering(s.misc))
		bucket = CLENT_HM_UNPROVEN;
	else if ((s.qvx | s.qvy | s.qvz) != 0)
		bucket = CLENT_HM_VEL;

	unsigned int k = ((unsigned int)s.classId * 2654435761u) % CLENT_HM_CLASSES;
	for (unsigned int probe = 0; probe < CLENT_HM_CLASSES; ++probe)
	{
		if (g_hmUsed[k] == false)
		{
			g_hmUsed[k] = true;
			memset(&g_hmClass[k], 0, sizeof(g_hmClass[k]));
			g_hmClass[k].classId = (unsigned short)s.classId;
		}
		if (g_hmClass[k].classId == s.classId)
		{
			++g_hmClass[k].n[bucket];
			return;
		}
		k = (k + 1) % CLENT_HM_CLASSES;
	}
	++g_hmOverflow[bucket];
}

unsigned int zx::netsnapshot::HeldMoveCensus(SnapHeldMoveClass* out, unsigned int max,
	unsigned int overflow[CLENT_HM_COUNT], unsigned int& players, bool reset)
{
	unsigned int n = 0;
	for (unsigned int k = 0; k < CLENT_HM_CLASSES; ++k)
	{
		if (g_hmUsed[k] && (n < max))
			out[n++] = g_hmClass[k];
	}
	memcpy(overflow, g_hmOverflow, sizeof(g_hmOverflow));
	players = g_hmPlayers;
	if (reset)
	{
		memset(g_hmUsed, 0, sizeof(g_hmUsed));
		memset(g_hmOverflow, 0, sizeof(g_hmOverflow));
		g_hmPlayers = 0;
	}
	return n;
}
static unsigned int g_delayBucket[5] = { 0, 0, 0, 0, 0 };
// The largest Z acceleration a measurement may report, and how far two
// readings may differ and still agree, which a landing's outlier fails.
#define CLENT_MAX_GRAV_ACCEL	(2.0 * 65536.0)
#define CLENT_GRAV_AGREE		(0.25 * 65536.0)

static unsigned int g_ballisticUpdates = 0, g_steeringUpdates = 0;
// The third class, counted: a classification with no counter cannot be
// told from one that never fires.
static unsigned int g_gravUpdates = 0;		// records naming the gravity class
static unsigned int g_gravExtrap = 0;		// samples extrapolated with a term
static unsigned int g_gravZeroAccel = 0;	// ...of which the term was zero
static unsigned int g_gravRejected = 0;		// refused: magnitude implausible
static unsigned int g_gravUnconfirmed = 0;	// held: awaiting a second, agreeing reading
static double g_gravAccelSum = 0.0;			// mean |accel|, units/tic/tic
static double g_gravAccelWorst = 0.0;

void zx::netsnapshot::GravStats(unsigned int & updates, unsigned int & extrap,
	unsigned int & zeroAccel, double & meanAccel, double & worstAccel,
	unsigned int & rejected, unsigned int & unconfirmed, bool reset)
{
	updates = g_gravUpdates; extrap = g_gravExtrap; zeroAccel = g_gravZeroAccel;
	rejected = g_gravRejected;
	unconfirmed = g_gravUnconfirmed;
	meanAccel = (g_gravExtrap > 0)
		? (g_gravAccelSum / (double)g_gravExtrap / 65536.0) : 0.0;
	worstAccel = g_gravAccelWorst / 65536.0;
	if (reset)
	{
		g_gravUpdates = g_gravExtrap = g_gravZeroAccel = 0;
		g_gravRejected = g_gravUnconfirmed = 0;
		g_gravAccelSum = 0.0; g_gravAccelWorst = 0.0;
	}
}
static unsigned int g_classEnded = 0, g_classBegan = 0, g_classChanged = 0;

// Installed by ghostactor.cpp; NULL in the self-tests.
static ClentLawGravityFn g_lawGravity = NULL;
static unsigned int g_lawExtrap = 0, g_lawNoFn = 0, g_lawDisagree = 0;

ClentLawGravityFn zx::netsnapshot::SetLawGravity(ClentLawGravityFn fn)
{
	const ClentLawGravityFn was = g_lawGravity;
	g_lawGravity = fn;
	return was;
}

void zx::netsnapshot::ClientLawStats(unsigned int & extrap, unsigned int & noLaw, unsigned int & disagree, bool reset)
{
	extrap = g_lawExtrap;
	noLaw = g_lawNoFn;
	disagree = g_lawDisagree;
	if (reset)
		g_lawExtrap = g_lawNoFn = g_lawDisagree = 0;
}
static ClentAnimatesFn g_animates = NULL;

ClentAnimatesFn zx::netsnapshot::SetAnimatesLookup(ClentAnimatesFn fn)
{
	const ClentAnimatesFn was = g_animates;
	g_animates = fn;
	return was;
}

// The window's worst rendered jumps, largest first, with entity, class, exit
// and gap.
#define CLENT_JUMPLOG	5
struct SnapJumpRec
{
	unsigned int units, id, classId;
	int gapTics;
	unsigned int posGapMS, recs;
	const char* branch;
};
static SnapJumpRec g_jumpLog[CLENT_JUMPLOG];
static const char* g_lastBranch = "?";
// Jumps by how long the entity had gone without a record, telling starvation
// from a culled entity's return.
static unsigned int g_jumpGapHist[CLENT_POP_COUNT][CLENT_INTERVAL_BUCKETS] = { { 0 } };
static unsigned int g_jumpGapUnits[CLENT_POP_COUNT][CLENT_INTERVAL_BUCKETS] = { { 0 } };
static unsigned int g_jumpCounted[CLENT_POP_COUNT] = { 0 };

// Record intervals, counted on arrival rather than on a jump. g_recTotal counts
// every arriving record; g_intervalTotal only those carrying position.
static unsigned int g_intervalHist[CLENT_POP_COUNT][CLENT_INTERVAL_BUCKETS] = { { 0 } };
static unsigned int g_intervalTotal[CLENT_POP_COUNT] = { 0 };
static unsigned int g_recTotal[CLENT_POP_COUNT] = { 0 };

static unsigned int g_localPlayerId = 0;

// Unbounded, so these initialisers set the sizes the assertions check.
const char* const CLENT_PopNames[] = { "other", "remote", "local" };
const char* const CLENT_IntervalNames[] = { "<4t", "<9t", "<18t", "<36t", "36t+" };

typedef char clent_assert_popnames[
	(sizeof(CLENT_PopNames) / sizeof(CLENT_PopNames[0]) == CLENT_POP_COUNT) ? 1 : -1];
typedef char clent_assert_intervalnames[
	(sizeof(CLENT_IntervalNames) / sizeof(CLENT_IntervalNames[0]) == CLENT_INTERVAL_BUCKETS) ? 1 : -1];

// Ids 1..MAXPLAYERS are player pawns by construction, so this costs a
// comparison rather than a findPointerByID per sample.
static inline unsigned int clent_Population(unsigned int id)
{
	if ((id == 0) || (id > MAXPLAYERS))
		return CLENT_POP_OTHER;
	return (id == g_localPlayerId) ? CLENT_POP_LOCAL : CLENT_POP_REMOTE;
}

// Frame arrival order from ApplyDelta's clock test, `sameNumber` counting
// a frame's later chunks, not duplicates.
static unsigned int g_frameOlder = 0;
static unsigned int g_frameOlderWorst = 0;
static unsigned int g_frameSameNumber = 0;

void zx::netsnapshot::FrameOrderStats(unsigned int& older, unsigned int& worst,
	unsigned int& sameNumber, bool reset)
{
	older = g_frameOlder;
	worst = g_frameOlderWorst;
	sameNumber = g_frameSameNumber;
	if (reset)
		g_frameOlder = g_frameOlderWorst = g_frameSameNumber = 0;
}

// Checksum verification, `failed` a divergence from what the server believes.
static unsigned int g_sumChecked = 0;
static unsigned int g_sumFailed = 0;
static unsigned int g_sumFirstBadId = 0;
// Newest for the entity, but a loss behind it may be unrepaired yet.
static unsigned int g_sumUnsettled = 0;
static unsigned int g_sumUnsettledFailed = 0;
// Older than a group the entity holds: describes a state no longer held.
static unsigned int g_sumLate = 0;

void zx::netsnapshot::ChecksumStats(unsigned int& checked, unsigned int& failed, unsigned int& firstBadId,
	unsigned int& unsettled, unsigned int& unsettledFailed, unsigned int& late, bool reset)
{
	checked = g_sumChecked;
	failed = g_sumFailed;
	firstBadId = g_sumFirstBadId;
	unsettled = g_sumUnsettled;
	unsettledFailed = g_sumUnsettledFailed;
	late = g_sumLate;
	if (reset)
	{
		g_sumChecked = g_sumFailed = 0;
		g_sumFirstBadId = 0;
		g_sumUnsettled = g_sumUnsettledFailed = g_sumLate = 0;
	}
}

// Set by the caller for the next ApplyDelta only.
static bool g_datagramGapBehind = false;

void zx::netsnapshot::SetDatagramGapBehind(bool gap)
{
	g_datagramGapBehind = gap;
}

// Frame-stamp refusals.
static unsigned int g_stampKept = 0;		// groups skipped by the frame rule
static unsigned int g_stampHeldBack = 0;	// taken into the model, not drawn
static unsigned int g_lateLeaves = 0;
static unsigned int g_lateEnters = 0;
static unsigned int g_lateEntersUnheld = 0;	// by a leave for an id not held
static unsigned int g_lateAbsent = 0;		// update older than the id's leave
static unsigned int g_absentAfterLeave = 0;	// update at or after it, or never seen

unsigned int zx::netsnapshot::LateEntersUnheld(bool reset)
{
	const unsigned int n = g_lateEntersUnheld;
	if (reset)
		g_lateEntersUnheld = 0;
	return n;
}

void zx::netsnapshot::StampStats(unsigned int& kept, unsigned int& heldBack, unsigned int& lateLeaves,
	unsigned int& lateEnters, unsigned int& lateAbsent, unsigned int& absentAfterLeave, bool reset)
{
	kept = g_stampKept;
	heldBack = g_stampHeldBack;
	lateLeaves = g_lateLeaves;
	lateEnters = g_lateEnters;
	lateAbsent = g_lateAbsent;
	absentAfterLeave = g_absentAfterLeave;
	if (reset)
		g_stampKept = g_stampHeldBack = g_lateLeaves = g_lateEnters
			= g_lateAbsent = g_absentAfterLeave = 0;
}
static unsigned int g_sampExtrap = 0, g_sampJump = 0;

static unsigned int g_sampExtrapBallistic = 0;
static unsigned int g_sampExtrapVel = 0, g_sampExtrapVelCapped = 0, g_sampExtrapVelMissile = 0;
static unsigned int g_sampExtrapVelCell = 0, g_sampExtrapVelCellEdge = 0;

// The present ids, and each id's slot in the list, for O(1) removal.
static unsigned int* g_presentIds = NULL;	// g_presentCount entries are valid
static int* g_presentSlot = NULL;			// -1 when the id is absent

static void clent_MarkPresent(unsigned int id)
{
	if (g_presentSlot[id] >= 0)
		return;
	g_presentSlot[id] = (int)g_presentCount;
	g_presentIds[g_presentCount++] = id;
}

static void clent_MarkAbsent(unsigned int id)
{
	const int slot = g_presentSlot[id];
	if (slot < 0)
		return;

	// Swap-remove: order is not meaningful to any consumer.
	const unsigned int last = g_presentIds[--g_presentCount];
	g_presentIds[slot] = last;
	g_presentSlot[last] = slot;
	g_presentSlot[id] = -1;
}
static unsigned int	g_streamVersion = ZSNAP_VERSION;

void zx::netsnapshot::SetStreamVersion(unsigned int version)
{
	g_streamVersion = version;
}

static void clent_Init()
{
	if (g_ghosts == NULL)
	{
		g_ghosts = new SnapGhost[SNAP_MAXID];
		memset(g_ghosts, 0, SNAP_MAXID * sizeof(SnapGhost));
		g_presentIds = new unsigned int[SNAP_MAXID];
		g_presentSlot = new int[SNAP_MAXID];
		g_enteredIds = new unsigned int[SNAP_MAXID];
		g_touchedIds = new unsigned int[SNAP_MAXID];
		g_leftAtMS = new unsigned int[SNAP_MAXID];
		memset(g_leftAtMS, 0, SNAP_MAXID * sizeof(unsigned int));
		memset(g_presentSlot, 0xFF, SNAP_MAXID * sizeof(int));	// all -1
	}
}

void zx::netsnapshot::ClientReset()
{
	clent_Init();
	memset(g_ghosts, 0, SNAP_MAXID * sizeof(SnapGhost));
	memset(g_presentSlot, 0xFF, SNAP_MAXID * sizeof(int));
	if (g_leftAtMS != NULL)
		memset(g_leftAtMS, 0, SNAP_MAXID * sizeof(unsigned int));
	g_presentCount = 0;
	g_enteredCount = 0;
	g_touchedCount = 0;
	g_streamVersion = ZSNAP_VERSION;
	g_haveServerTic = false;	// tic numbering does not carry across a map
	++g_streamEpoch;
	ClockReset();
}

// The joined map's stamp from SnapJoin, the only one accepted, since the next
// map's frames can arrive before this one is torn down.
static BYTE			g_joinedStamp = 0;
static bool			g_mapJoined = false;
// Set when SnapJoin announced a frame width this build has no encoding for,
// so the map's payloads are refused rather than misread.
static bool			g_mapFrameUnknown = false;
static BYTE			g_mapFrameWire[3] = { SNAP_FRAME_NARROW, SNAP_FRAME_NARROW, SNAP_FRAME_NARROW };
static unsigned int	g_mapStampStale = 0;	// from a map already torn down
static unsigned int	g_mapStampAhead = 0;	// from a map not yet loaded
static unsigned int	g_beforeJoin = 0;

// The joined map's sound-table answer, cleared with the map stamp.
static bool			g_soundIdsKnown = false;
static bool			g_soundIdsMatch = false;
static unsigned int	g_soundIdsServerCount = 0, g_soundIdsLocalCount = 0;
static unsigned int	g_soundIdsPlayed = 0, g_soundIdsDropped = 0;
// Sounds stamped for another map, which carry that map's table and its mirrors.
static unsigned int	g_soundOtherMap = 0;

bool zx::netsnapshot::AcceptMapStamp(BYTE stamp)
{
	if (g_mapJoined == false)
	{
		++g_beforeJoin;
		return false;
	}
	if (g_mapFrameUnknown)
		return false;
	const int ahead = ComputeMapStampAhead(stamp, g_joinedStamp);
	if (ahead < 0)
		++g_mapStampStale;
	else if (ahead > 0)
		++g_mapStampAhead;
	return ahead == 0;
}

bool zx::netsnapshot::MapStampCurrent(BYTE stamp)
{
	return g_mapJoined && (g_mapFrameUnknown == false) && (stamp == g_joinedStamp);
}

// The configuration line at a connection's first delta, armed by a connect or
// a demo playback only.
namespace zx { namespace netsnapshot {
void ReportCvars();	// capture.cpp, both ends
}} // namespace zx::netsnapshot
static bool g_cvarsReported = true;

// The joined map's position frame; codec calls on the client read it.
static SnapFrame g_mapFrame = FrameNarrow();

void zx::netsnapshot::ResetMapStamp()
{
	g_cvarsReported = false;
	g_mapJoined = false;
	g_mapFrame = FrameNarrow();
	g_mapFrameUnknown = false;
	for (unsigned int k = 0; k < 3; ++k)
		g_mapFrameWire[k] = SNAP_FRAME_NARROW;
	g_soundIdsKnown = false;
	g_soundIdsMatch = false;
}

// Bumped by every SnapJoin, where a new stream begins.
static unsigned int g_joinCount = 0;

unsigned int zx::netsnapshot::JoinCount()
{
	return g_joinCount;
}

void zx::netsnapshot::AnchorMapStamp(BYTE stamp)
{
	++g_joinCount;
	g_joinedStamp = stamp;
	g_mapJoined = true;
	RecorderMapJoined();
}

void zx::netsnapshot::SetMapFrame(int originX, int originY, int originZ, int widthX, int widthY,
	int widthZ)
{
	SnapFrame f = FrameNarrow();
	const int origin[3] = { originX, originY, originZ };
	const int width[3] = { widthX, widthY, widthZ };
	g_mapFrameUnknown = false;
	for (unsigned int k = 0; k < 3; ++k)
	{
		f.origin[k] = origin[k];
		g_mapFrameWire[k] = (BYTE)width[k];
		if (FrameWidthKnown((unsigned int)width[k]))
			f.width[k] = (BYTE)width[k];
		else
			g_mapFrameUnknown = true;
	}
	// No reader can size an unknown width, so the map is refused: an empty world
	// rather than every position misread.
	if (g_mapFrameUnknown)
	{
		Printf("snapshot: the server's frame is %u %u %u bytes an axis, which this build "
			"does not read; this map's snapshots are refused\n", g_mapFrameWire[0],
			g_mapFrameWire[1], g_mapFrameWire[2]);
	}
	UseMapFrame(f);
}

void zx::netsnapshot::UseMapFrame(const SnapFrame& frame)
{
	g_mapFrame = frame;
	g_mapFrame.pClamped = NULL;
}

const SnapFrame& zx::netsnapshot::ClientMapFrame()
{
	return g_mapFrame;
}

void zx::netsnapshot::ClientFrameWire(BYTE w[3])
{
	for (unsigned int k = 0; k < 3; ++k)
		w[k] = g_mapFrameWire[k];
}

fixed_t zx::netsnapshot::ReadFramePos(BYTESTREAM_s* pByteStream, unsigned int k)
{
	// Under an unknown width the announced bytes are skipped, so the rest of the
	// command still reads in step.
	if (g_mapFrameUnknown)
	{
		for (unsigned int b = 0; b < g_mapFrameWire[k]; ++b)
			pByteStream->ReadByte();
		return 0;
	}
	const unsigned int w = PosBytes(g_mapFrame, k);
	BYTE rel[SNAP_FRAME_WIDEST];
	bool bPastEnd = false;
	for (unsigned int b = 0; b < w; ++b)
	{
		const int v = pByteStream->ReadByte();
		bPastEnd = bPastEnd || (v < 0);
		rel[b] = (BYTE)v;
	}
	// Past the end the value is -1, as ReadShort returned.
	const SDWORD r = bPastEnd ? -1 : (SDWORD)ReadSigned(rel, w);
	return DequantPos(PosAbs(g_mapFrame, k, r));
}

bool zx::netsnapshot::JoinedStamp(BYTE& stamp)
{
	stamp = g_joinedStamp;
	return g_mapJoined;
}

void zx::netsnapshot::SetJoinedStamp(bool bJoined, BYTE stamp)
{
	g_joinedStamp = stamp;
	g_mapJoined = bJoined;
}

void zx::netsnapshot::SetSoundTable(BYTE stamp, bool match, unsigned int serverCount, unsigned int localCount)
{
	// It follows SnapJoin on the reliable stream; a table for any other map
	// says nothing about this one.
	if ((g_mapJoined == false) || (stamp != g_joinedStamp))
		return;
	g_soundIdsKnown = true;
	g_soundIdsMatch = match;
	g_soundIdsServerCount = serverCount;
	g_soundIdsLocalCount = localCount;
}

BYTE zx::netsnapshot::SoundIdsAnswer()
{
	if ((g_mapJoined == false) || (g_soundIdsKnown == false))
		return 0;
	return (BYTE)(SNAP_SOUNDIDS_ANSWERED | (g_soundIdsMatch ? SNAP_SOUNDIDS_MATCH : 0)
		| (g_joinedStamp & SNAP_SOUNDIDS_STAMP));
}

bool zx::netsnapshot::SoundIdUsable(unsigned int id)
{
	// At or above the hashed count is where this client's own additions sit, and
	// a refused map's sounds have no mirrors and no positions to play at.
	return g_mapJoined && (g_mapFrameUnknown == false) && g_soundIdsKnown
		&& g_soundIdsMatch && (id > 0) && (id < g_soundIdsServerCount);
}

void zx::netsnapshot::CountSoundId(bool bPlayed)
{
	if (bPlayed)
		++g_soundIdsPlayed;
	else
		++g_soundIdsDropped;
}

void zx::netsnapshot::SoundIdStats(unsigned int& played, unsigned int& dropped, unsigned int& serverCount,
	unsigned int& localCount, bool& known, bool& match, bool reset)
{
	played = g_soundIdsPlayed;
	dropped = g_soundIdsDropped;
	serverCount = g_soundIdsServerCount;
	localCount = g_soundIdsLocalCount;
	known = g_soundIdsKnown;
	match = g_soundIdsMatch;
	if (reset)
		g_soundIdsPlayed = g_soundIdsDropped = 0;
}

void zx::netsnapshot::CountSoundOtherMap()
{
	++g_soundOtherMap;
}

unsigned int zx::netsnapshot::SoundOtherMap(bool reset)
{
	const unsigned int n = g_soundOtherMap;
	if (reset)
		g_soundOtherMap = 0;
	return n;
}

unsigned int zx::netsnapshot::MapStampStale(bool reset)
{
	const unsigned int n = g_mapStampStale;
	if (reset)
		g_mapStampStale = 0;
	return n;
}

unsigned int zx::netsnapshot::MapStampAhead(bool reset)
{
	const unsigned int n = g_mapStampAhead;
	if (reset)
		g_mapStampAhead = 0;
	return n;
}

unsigned int zx::netsnapshot::BeforeJoin(bool reset)
{
	const unsigned int n = g_beforeJoin;
	if (reset)
		g_beforeJoin = 0;
	return n;
}

void zx::netsnapshot::ForgetWorld()
{
	// Before the early return: no ghost table says nothing about the stream.
	g_mapJoined = false;

	// No clent_Init(), since the teardown also runs where no table exists.
	if (g_ghosts == NULL)
		return;

	memset(g_ghosts, 0, SNAP_MAXID * sizeof(SnapGhost));
	memset(g_presentSlot, 0xFF, SNAP_MAXID * sizeof(int));
	if (g_leftAtMS != NULL)
		memset(g_leftAtMS, 0, SNAP_MAXID * sizeof(unsigned int));
	g_presentCount = 0;
	g_enteredCount = 0;
	g_touchedCount = 0;
	g_streamVersion = ZSNAP_VERSION;
	g_haveServerTic = false;	// tic numbering does not carry across a map
	++g_streamEpoch;
	ClockReset();

	// Jump entries name an id and a class and the next map reuses both, so a
	// surviving entry reports a position delta between two different actors.
	memset(g_jumpLog, 0, sizeof(g_jumpLog));
}

unsigned int zx::netsnapshot::PresentCount()
{
	return g_presentCount;
}

unsigned int zx::netsnapshot::ListPresent(unsigned int* pIds, unsigned int maxIds)
{
	clent_Init();
	if ((pIds == NULL) || (maxIds == 0))
		return 0;

	const unsigned int n = (g_presentCount < maxIds) ? g_presentCount : maxIds;
	memcpy(pIds, g_presentIds, n * sizeof(unsigned int));
	return n;
}

// The newest GAME tic this client has been told about: a frame counts
// pumps, and anything measuring elapsed world time takes this one.
int zx::netsnapshot::NewestGameTic()
{
	return g_newestGameTic;
}

bool zx::netsnapshot::PresentIds(const unsigned int*& ids, unsigned int& count, int& newestFrame)
{
	ids = g_presentIds;
	count = (g_presentIds != NULL) ? g_presentCount : 0;
	newestFrame = g_newestServerTic;
	return g_haveServerTic && (g_presentIds != NULL);
}

// Writes the present ghosts as enter records, a recording's baseline, through
// WriteRecord; it has no history, so a replay's first moments differ.
unsigned int zx::netsnapshot::ExportBaseline(BYTE * pOut, unsigned int maxBytes, int& serverTic)
{
	unsigned int written = 0;
	serverTic = 0;
	if ((g_ghosts == NULL) || (pOut == NULL))
		return 0;

	// One payload, so one id base, starting fresh - the replay decodes this
	// with a cursor reset the same way.
	SnapIdCursor ids;
	IdCursorReset(ids);

		for (unsigned int k = 0; k < g_presentCount; ++k)
		{
			const unsigned int id = g_presentIds[k];
			if (id >= SNAP_MAXID)
				continue;
			
			const SnapGhost & g = g_ghosts[id];
			if (g.present == false)
				continue;
			
			if (written + SNAP_MAX_RECORD_BYTES > maxBytes)
				break;
			
			const SnapGhostSample & newest = g.hist[g.head];
			written += WriteRecord(pOut + written, g_mapFrame, (WORD)id, ids,
			(BYTE)(SNAPGRP_ALL | SNAPGRP_ENTER), newest.state.classId, newest.state,
			NULL, g.targetNetID);
			// The newest server tic seen, so the replay's first record carries a
			// plausible frame number rather than zero.
			if (newest.serverTic > serverTic)
				serverTic = newest.serverTic;
	}
	return written;
}

const unsigned int* zx::netsnapshot::PresentIds(unsigned int& count)
{
	clent_Init();
	count = g_presentCount;
	return g_presentIds;
}

// Whether this id left within the last `withinMS` of packet time.
bool zx::netsnapshot::LeftWithin(unsigned int id, unsigned int withinMS)
{
	if ((g_leftAtMS == NULL) || (id >= SNAP_MAXID) || (g_leftAtMS[id] == 0))
		return false;

	// Both sides carry the +1 bias, and the >= guard keeps a reordered
	// packet's leave from wrapping.
	const unsigned int now = g_lastApplyMS + 1u;
	return (now >= g_leftAtMS[id]) && ((now - g_leftAtMS[id]) <= withinMS);
}

// The last position of an id that left within `withinMS` of arrival time, not
// gated on `present`, which is the question.
bool zx::netsnapshot::DepartedOrigin(unsigned int id, unsigned int withinMS,
	fixed_t& x, fixed_t& y, fixed_t& z)
{
	clent_Init();
	if (LeftWithin(id, withinMS) == false)
		return false;

	const SnapGhost& g = g_ghosts[id];
	x = DequantPos(g.nextState.qx);
	y = DequantPos(g.nextState.qy);
	z = DequantPos(g.nextState.qz);
	return true;
}

bool zx::netsnapshot::GhostPresent(unsigned int id)
{
	clent_Init();
	return (id < SNAP_MAXID) && (g_ghosts[id].present != false);
}

bool zx::netsnapshot::GhostPosition(unsigned int id, fixed_t& x, fixed_t& y, fixed_t& z)
{
	clent_Init();
	if ((id >= SNAP_MAXID) || (g_ghosts[id].present == false))
		return false;

	const SnapGhost& g = g_ghosts[id];
	x = DequantPos(g.nextState.qx);
	y = DequantPos(g.nextState.qy);
	z = DequantPos(g.nextState.qz);
	return true;
}

bool zx::netsnapshot::GhostDebug(unsigned int id, SnapGhostDebug& out)
{
	clent_Init();
	if ((id >= SNAP_MAXID) || (g_ghosts[id].present == false))
		return false;

	const SnapGhost& g = g_ghosts[id];
	out.classId = g.nextState.classId;
	out.spriteframe = g.nextState.spriteframe;
	out.qstate = g.nextState.qstate;
	out.prevMS = g.prevMS;
	out.nextMS = g.nextMS;
	out.prevTic = g.prevTic;
	out.nextTic = g.nextTic;
	out.x = DequantPos(g.nextState.qx);
	out.y = DequantPos(g.nextState.qy);
	out.z = DequantPos(g.nextState.qz);
	out.effects = g.nextState.effects;
	out.misc = g.nextState.misc;
	out.posX = DequantPos(g.hist[g.head].state.qx);
	out.posY = DequantPos(g.hist[g.head].state.qy);
	out.posZ = DequantPos(g.hist[g.head].state.qz);
	out.posTic = g.hist[g.head].posTic;
	out.velX = DequantVel(g.hist[g.head].state.qvx);
	out.velY = DequantVel(g.hist[g.head].state.qvy);
	out.velZ = DequantVel(g.hist[g.head].state.qvz);
	return true;
}

unsigned int zx::netsnapshot::GhostsWithEffects()
{
	clent_Init();
	unsigned int n = 0;
	for (unsigned int i = 0; i < g_presentCount; ++i)
	{
		const unsigned int id = g_presentIds[i];
		if ((id < SNAP_MAXID) && g_ghosts[id].present
			&& (g_ghosts[id].nextState.effects != 0))
			++n;
	}
	return n;
}

unsigned short zx::netsnapshot::GhostClassId(unsigned int id)
{
	clent_Init();
	if ((id >= SNAP_MAXID) || (g_ghosts[id].present == false))
		return 0;
	return g_ghosts[id].nextState.classId;
}

unsigned short zx::netsnapshot::GhostTarget(unsigned int id, bool& resolved, bool& changed)
{
	clent_Init();
	resolved = false;
	changed = false;
	if (id >= SNAP_MAXID)
		return 0;
	resolved = g_ghosts[id].targetResolved;
	changed = g_ghosts[id].targetChanged;
	g_ghosts[id].targetChanged = false;
	return g_ghosts[id].targetNetID;
}

void zx::netsnapshot::GhostTargetResolved(unsigned int id)
{
	if ((g_ghosts != NULL) && (id < SNAP_MAXID))
		g_ghosts[id].targetResolved = true;
}

//*****************************************************************************
//	DELTA APPLICATION (the decoder state machine)

static void clent_SetError(char* pszError, unsigned int errorLen, unsigned int errorCount,
	const char* pszFormat, unsigned int a, unsigned int b)
{
	// Only the first error is captured in text form.
	if ((pszError == NULL) || (errorLen == 0) || (errorCount != 0))
		return;
	snprintf(pszError, errorLen, pszFormat, a, b);
	pszError[errorLen - 1] = '\0';
}

// Branch events received, by whether the ghost existed (orphans: 0).
static unsigned int g_evApplied = 0;
static unsigned int g_evStale = 0;	// dropped: older than the state it would overwrite
// Events whose STATE was newer than what the ghost held while their POSITION
// was not: applied for the state only.
static unsigned int g_evStalePos = 0;
static unsigned int g_evNotAnimated = 0;	// dropped: mirror cannot animate out of it
static unsigned int g_evDuplicate = 0;	// dropped: a copy of one already applied

// An event overlays the drawn sample, not the model, until a record carries
// those groups.

// Pushes where that kept a value differing from the model: what reverted
// before.
static unsigned int g_evCarried = 0;
static unsigned int g_evCarriedPos = 0;
static unsigned int g_evCarriedState = 0;

// Applied events by age and by the duration their state had left, `expired`
// counting those over before they arrived.
static unsigned int g_evAge[5] = { 0, 0, 0, 0, 0 };
static unsigned int g_evDuration[4] = { 0, 0, 0, 0 };
static unsigned int g_evExpired = 0;
void zx::netsnapshot::EventAgeStats(unsigned int* age, unsigned int* duration, unsigned int& expired)
{
	for (unsigned int i = 0; i < 5; ++i)
		age[i] = g_evAge[i];

	for (unsigned int i = 0; i < 4; ++i)
		duration[i] = g_evDuration[i];

	expired = g_evExpired;
}

static unsigned int g_evOrphan = 0;

// Payloads abandoned on an unknown fixed-length event type, records lost.
static unsigned int g_evUnknownType = 0;

// Self-describing records skipped as newer than this build.
static unsigned int g_evSkippedNewer = 0;

// Each missile's branch: stepped, extrapolating, or frozen at the cap.
static unsigned int g_misHeldNoBit = 0;
static unsigned int g_misExtrap = 0;
static unsigned int g_misExtrapCapped = 0;
static unsigned int g_misMaxAgeMS = 0;

// Freshest age seen, split by whether the missile carried the ballistic bit.
// 0xFFFFFFFF = none seen.
static unsigned int g_misMinAgeBit = 0xFFFFFFFF;
static unsigned int g_misMinAgeNoBit = 0xFFFFFFFF;

// Where the staleness comes from, since "capped" alone cannot say.
static SnapMissileCensus g_misCensus = { { 0, 0, 0, 0, 0 }, 0, 0, 0, 0, 0 };
static fixed_t g_viewX = 0, g_viewY = 0;
static bool g_viewValid = false;

static ClentPSpriteFn g_psprSink = NULL;
static ClentPropsFn g_propsSink = NULL;
static unsigned int g_propsApplied = 0, g_propsBad = 0;

ClentPropsFn zx::netsnapshot::SetPropsSink(ClentPropsFn fn)
{
	const ClentPropsFn was = g_propsSink;
	g_propsSink = fn;
	return was;
}

void zx::netsnapshot::PropsEventStats(unsigned int & applied, unsigned int & bad, bool reset)
{
	applied = g_propsApplied;
	bad = g_propsBad;
	if (reset)
		g_propsApplied = g_propsBad = 0;
}

ClentPSpriteFn zx::netsnapshot::SetPSpriteSink(ClentPSpriteFn fn)
{
	const ClentPSpriteFn was = g_psprSink;
	g_psprSink = fn;
	return was;
}

static ClentLocalFn g_localSink = NULL;
static unsigned int g_localApplied = 0, g_localNoSink = 0;

// The game tic of the frame being applied now, not g_newestGameTic, which the
// local sink's ordering guard needs (cl_entities.md).
static int			g_applyingGameTic = 0;
// Its frame number, the ordering clock (9.2), for the mover sink.
static int			g_applyingServerTic = 0;

ClentLocalFn zx::netsnapshot::SetLocalSink(ClentLocalFn fn)
{
	const ClentLocalFn was = g_localSink;
	g_localSink = fn;
	return was;
}

static ClentMoverFn g_moverSink = NULL;
static unsigned int g_moverApplied = 0;
static unsigned int g_moverNoSink = 0;
static unsigned int g_moverBad = 0;

ClentMoverFn zx::netsnapshot::SetMoverSink(ClentMoverFn fn)
{
	const ClentMoverFn was = g_moverSink;
	g_moverSink = fn;
	return was;
}

int zx::netsnapshot::ApplyingServerTic(void)
{
	return g_applyingServerTic;
}

unsigned int zx::netsnapshot::StreamEpoch(void)
{
	return g_streamEpoch;
}

void zx::netsnapshot::ClientMoverStats(unsigned int & applied, unsigned int & noSink,
	unsigned int & bad, bool reset)
{
	applied = g_moverApplied;
	noSink = g_moverNoSink;
	bad = g_moverBad;
	if (reset)
		g_moverApplied = g_moverNoSink = g_moverBad = 0;
}

void zx::netsnapshot::ClientLocalStats(unsigned int & applied, unsigned int & noSink, bool reset)
{
	applied = g_localApplied;
	noSink = g_localNoSink;
	if (reset)
		g_localApplied = g_localNoSink = 0;
}

// Psprite records with no sink installed, and those for a player not held.
static unsigned int g_psprApplied = 0, g_psprNoSink = 0;

void zx::netsnapshot::ClientPSpriteStats(unsigned int & applied, unsigned int & noSink, bool reset)
{
	applied = g_psprApplied;
	noSink = g_psprNoSink;
	if (reset)
		g_psprApplied = g_psprNoSink = 0;
}

void zx::netsnapshot::SetLocalPlayerId(unsigned int id)
{
	g_localPlayerId = id;
}

void zx::netsnapshot::SetViewPoint(fixed_t x, fixed_t y)
{
	g_viewX = x; g_viewY = y; g_viewValid = true;
}

void zx::netsnapshot::MissileCensus(SnapMissileCensus & out, bool reset)
{
	out = g_misCensus;
	if (reset)
		memset(&g_misCensus, 0, sizeof(g_misCensus));
}

void zx::netsnapshot::MissileStats(unsigned int& heldNoBit, unsigned int& extrap, unsigned int& capped, unsigned int& maxAgeMS,
	unsigned int& minBit, unsigned int& minNoBit, unsigned int& sampBallistic)
{
	heldNoBit = g_misHeldNoBit; extrap = g_misExtrap;
	capped = g_misExtrapCapped; maxAgeMS = g_misMaxAgeMS;
	sampBallistic = g_sampExtrapBallistic;
	// 0xFFFFFFFF means none seen; the printer shows that as "none".
	minBit = g_misMinAgeBit;
	minNoBit = g_misMinAgeNoBit;
}

static SnapBlindPredict g_blind;

void zx::netsnapshot::BlindPredict(SnapBlindPredict & out, bool reset)
{
	out = g_blind;
	if (reset)
		memset(&g_blind, 0, sizeof(g_blind));
}

void zx::netsnapshot::MissileStatsReset()
{
	g_misHeldNoBit = g_misExtrap = g_misExtrapCapped = g_misMaxAgeMS = 0;
	g_misMinAgeBit = g_misMinAgeNoBit = 0xFFFFFFFF;
	memset(&g_misCensus, 0, sizeof(g_misCensus));
}

// EVERY out-parameter is assigned here, unconditionally: a reference
// out-param never written produces no diagnostic and prints a plausible
// figure.
void zx::netsnapshot::ClientEventStats(unsigned int& applied, unsigned int& orphan, unsigned int& unknown, unsigned int& stale, unsigned int& notAnimated, unsigned int& stalePos, unsigned int& duplicate)
{
	duplicate = g_evDuplicate;
	applied = g_evApplied; orphan = g_evOrphan;
	// Reported together, the lost and the skipped apart on the line.
	unknown = g_evUnknownType + g_evSkippedNewer;
	stale = g_evStale;
	notAnimated = g_evNotAnimated;
	stalePos = g_evStalePos;
}

void zx::netsnapshot::EventCarryStats(unsigned int& carried, unsigned int& pos, unsigned int& state)
{
	carried = g_evCarried;
	pos = g_evCarriedPos;
	state = g_evCarriedState;
}

void zx::netsnapshot::ClientEventStatsReset()
{
	g_evApplied = g_evOrphan = g_evUnknownType = g_evStale = 0;
	g_evSkippedNewer = 0;
	g_evStalePos = 0;
	g_evNotAnimated = 0;
	g_evDuplicate = 0;
	g_evCarried = g_evCarriedPos = g_evCarriedState = 0;
	memset(g_evAge, 0, sizeof(g_evAge));
	memset(g_evDuration, 0, sizeof(g_evDuration));
	g_evExpired = 0;
}

// Applies one event to the ghost's newest sample, overwriting rather than
// pushing, `pPayload` being past any length byte.
static void clent_ApplyEvent(WORD id, BYTE type, const BYTE * pPayload,
	unsigned int payloadBytes, unsigned int wallMS)
{
	// AHEAD of the id and presence tests: a mover names no entity, its id field
	// is zero by design, and either test would refuse every record.
	if ((type & SNAPEV_TYPEMASK) == SNAPEV_MOVER)
	{
		if (g_moverSink == NULL)
		{
			++g_moverNoSink;
			return;
		}
		const unsigned int hdrBytes = MoverHeaderBytes(g_streamVersion);
		if (payloadBytes < hdrBytes)
		{
			++g_moverBad;
			return;
		}
		BYTE cls = 0, mflags = 0, gen = 0;
		WORD index = 0;
		ReadMoverHeader(pPayload, hdrBytes, cls, mflags, index, gen);
		// A polyobject's length is walked from its body, bounded by the payload.
		const unsigned int bodyBytes = MoverRecordBodyBytes(g_mapFrame, cls, mflags,
			pPayload + hdrBytes, payloadBytes - hdrBytes);
		// An unknown class or a disagreeing length loses this record only,
		// checked here so the sink need not bound the body.
		if ((bodyBytes == 0)
			|| ((hdrBytes + bodyBytes) != payloadBytes))
		{
			++g_moverBad;
			return;
		}
		g_moverSink(cls, mflags, gen, index, pPayload + hdrBytes, bodyBytes);
		++g_moverApplied;
		return;
	}

	if (id >= SNAP_MAXID)
		return;

	// Ahead of the presence test, as the local player has no ghost to test.
	if ((type & SNAPEV_TYPEMASK) == SNAPEV_LOCAL)
	{
		if (g_localSink == NULL)
		{
			++g_localNoSink;
			return;
		}
		SnapLocalState st;
		ReadLocal(pPayload, g_mapFrame, st);
		g_localSink(id, st, g_applyingGameTic);
		++g_localApplied;
		return;
	}

	SnapGhost & g = g_ghosts[id];
	if (g.present == false)
	{
		++g_evOrphan;
		return;
	}

	if ((type & SNAPEV_TYPEMASK) == SNAPEV_PROPS)
	{
		// The length byte is behind the caller, so `bytes` is the body alone.
		SnapProps props;
		if (ReadProps(pPayload, payloadBytes, props) == false)
		{
			++g_propsBad;
			return;
		}
		if (g_propsSink != NULL)
			g_propsSink(id, props);
		++g_propsApplied;
		return;
	}

	if ((type & SNAPEV_TYPEMASK) == SNAPEV_PSPRITE)
	{
		if (g_psprSink == NULL)
		{
			++g_psprNoSink;
			return;
		}
		SnapPSpriteState st;
		ReadPSprite(pPayload, st);
		g_psprSink(id, st);
		++g_psprApplied;
		return;
	}

	if ((type & SNAPEV_TYPEMASK) != SNAPEV_BRANCH)
		return;
	// Only a mirror running the state machine can leave an event's state.
	if ((g_animates != NULL) && (g_animates(id) == false))
	{
		++g_evNotAnimated;
		return;
	}

	SnapEntityState probe;
	const WORD evTic = ReadEvent(pPayload, g_mapFrame, type, probe);

	// An event older than a group's state leaves that group alone, judged
	// per group on the game clock in a signed 16-bit difference.
	const int evAt = (int)g_newestGameTic
		- (int)(SWORD)((WORD)g_newestGameTic - evTic);
	// A copy of the last event applied is dropped, or it would resync a
	// death that has played out.
	if (g.haveEvent && (evAt == g.lastEventTic) && (probe.qstate == g.lastEventState))
	{
		++g_evDuplicate;
		return;
	}
	const bool bPosFresh = (evAt >= g.hist[g.head].posTic);
	const bool bStateFresh = (evAt >= g.hist[g.head].appearTic);
	if ((bPosFresh == false) && (bStateFresh == false))
	{
		++g_evStale;
		return;
	}

	// The age census, against the state's own duration, since an event may
	// describe a state already over.
	{
		const int age = (int)(SWORD)((WORD)g_newestServerTic - evTic);
		const unsigned int b = (age < 2) ? 0 : (age < 4) ? 1 : (age < 7) ? 2 : (age < 12) ? 3 : 4;
		++g_evAge[b];
		// qtics is what REMAINED of the state when the branch was observed, so
		// age >= qtics means the state has expired by the time it lands.
		if (age >= (int)probe.qtics)
			++g_evExpired;

		const unsigned int t = (probe.qtics < 3) ? 0 : (probe.qtics < 7) ? 1 : (probe.qtics < 15) ? 2 : 3;
		++g_evDuration[t];
	}

	SnapEntityState & st = g.hist[g.head].state;
	if (bStateFresh)
	{
		st.qstate = probe.qstate;
		st.qtics = probe.qtics;
	}
	if (bPosFresh)
	{
		st.qx = probe.qx;
		st.qy = probe.qy;
		st.qz = probe.qz;
		// The position clock moves with the position, or the next event would
		// be judged against the moment a record last carried one.
		g.hist[g.head].posTic = evAt;
		// posMS too, back-dated by the event's age and clamped, or the
		// ballistic exit adds the withheld stretch.
		const int ageTics = g_applyingGameTic - evAt;
		long long atMS = (long long)wallMS - (long long)ageTics * 1000 / TICRATE;
		if (atMS < (long long)g.hist[g.head].posMS)
			atMS = (long long)g.hist[g.head].posMS;
		if (atMS > (long long)wallMS)
			atMS = (long long)wallMS;
		g.hist[g.head].posMS = (unsigned int)atMS;
	}
	else
	{
		++g_evStalePos;
	}
	// The appearance clock takes the event's own tic, monotonically.
	if (bStateFresh && (evAt > g.hist[g.head].appearTic))
		g.hist[g.head].appearTic = evAt;
	++g.eventSeq;
	g.lastEventTic = evAt;
	g.lastEventState = probe.qstate;
	g.haveEvent = true;
	++g_evApplied;
}

static void clent_ClockFeed(int gameTic, unsigned int wallMS);

const unsigned int* zx::netsnapshot::TouchedIds(unsigned int& count)
{
	count = (g_touchedIds != NULL) ? g_touchedCount : 0;
	return g_touchedIds;
}

unsigned int zx::netsnapshot::ApplyDelta(const BYTE* pData, unsigned int size,
	int serverTic, int gameTic, unsigned int wallMS, char* pszError, unsigned int errorLen,
	SnapApplyStats* pStats)
{
	clent_Init();
	if (g_cvarsReported == false)
	{
		g_cvarsReported = true;
		ReportCvars();
	}

	// This payload's own clock, captured before the monotonic newest-seen
	// update below can diverge from it.
	g_applyingGameTic = gameTic;
	g_applyingServerTic = serverTic;

	// One-shot, so an offline caller never inherits a live datagram's flag.
	const bool gapBehind = g_datagramGapBehind;
	g_datagramGapBehind = false;

	// The newest arrival time for LeftWithin, monotonic.
	if (wallMS > g_lastApplyMS)
		g_lastApplyMS = wallMS;

	// The clock stays monotonic, older and same-number payloads counted.
	if (g_haveServerTic && (serverTic < g_newestServerTic))
	{
		++g_frameOlder;
		const unsigned int lag = (unsigned int)(g_newestServerTic - serverTic);
		if (lag > g_frameOlderWorst)
			g_frameOlderWorst = lag;
	}
	else if (g_haveServerTic && (serverTic == g_newestServerTic))
	{
		++g_frameSameNumber;
	}
	if ((g_haveServerTic == false) || (serverTic > g_newestServerTic))
	{
		g_newestServerTic = serverTic;
		g_newestGameTic = gameTic;
		g_haveServerTic = true;
	}
	clent_ClockFeed(gameTic, wallMS);

	if (pszError != NULL && errorLen > 0)
		pszError[0] = '\0';
	if (pStats != NULL)
		*pStats = SnapApplyStats();

	// The entered list describes this payload only, as the touched list does.
	g_enteredCount = 0;
	g_touchedCount = 0;

	unsigned int errors = 0;
	unsigned int off = 0;

	// The id base for this payload, which restarts with each datagram.
	SnapIdCursor ids;
	IdCursorReset(ids);

	while (off < size)
	{
		WORD id = 0;
		const unsigned int idBytes = ReadRecordId(pData + off, size - off,
			id, ids);
		if (idBytes == 0)
		{
			// A bad id leaves the rest of the payload unparseable.
			clent_SetError(pszError, errorLen, errors,
				"malformed entity id at offset %u", off, 0);
			++errors;
			return errors;
		}
		off += idBytes;
		if (off + 1 > size)
		{
			clent_SetError(pszError, errorLen, errors, "truncated entity header at offset %u", off, 0);
			++errors;
			return errors;
		}
		const BYTE rawMask = pData[off];
		off += 1;

		// Every record naming this id, events included, so a still entity
		// does not read as absent.
		++g_recTotal[clent_Population(id)];

		// The event bit before the groups, whose flags its type overlaps.
		if ((rawMask & SNAPGRP_EVENT) != 0)
		{
			const BYTE evType = (BYTE)(rawMask & SNAPEV_TYPEMASK);
			unsigned int evBytes = 0;
			bool bSkip = false;
			const int evLen = EventRecordBytes(g_mapFrame, evType, pData + off,
				size - off, evBytes, bSkip);
			if (evLen == SNAPEVLEN_UNKNOWN)
			{
				// An unknown fixed-length type abandons the payload, which
				// cannot resume.
				++g_evUnknownType;
				clent_SetError(pszError, errorLen, errors,
					"unknown event type %u at offset %u", evType, off);
				++errors;
				return errors;
			}
			if (evLen == SNAPEVLEN_BAD)
			{
				clent_SetError(pszError, errorLen, errors,
					"truncated event payload at offset %u", off, 0);
				++errors;
				return errors;
			}
			if (bSkip)
			{
				++g_evSkippedNewer;	// self-describing, and newer than this build
			}
			else
			{
				// A self-describing payload starts past its length byte, which
				// evBytes counts.
				const unsigned int lead =
					EventSelfDescribing(evType) ? 1u : 0u;
				clent_ApplyEvent(id, evType, pData + off + lead,
					evBytes - lead, wallMS);
			}
			off += evBytes;
			continue;
		}


		const BYTE mask = (BYTE)(rawMask & SNAPGRP_GROUPMASK);
		// Whether a record carries a classId has ONE rule, shared with the
		// writer, or the parse misaligns: the SNAPGRP_ENTER flag.
		const bool entering = (rawMask & SNAPGRP_ENTER) != 0;

		// The wire id indexes g_ghosts directly, which snap_assert_id_space
		// guarantees, this net acting only on a shrunk table.
#if SNAP_MAXID < 0x10000
		if (id >= SNAP_MAXID)
		{
			clent_SetError(pszError, errorLen, errors, "entity id %u out of range (max %u)",
				id, SNAP_MAXID - 1);
			++errors;
			return errors;
		}
#endif

		SnapGhost& g = g_ghosts[id];

		if (mask == 0)
		{
			// A leave, idempotent as the server re-sends it until acked.
			if (g.present == false)
			{
				if (pStats != NULL)
					pStats->redundant++;
				// Stamped all the same, so an enter it overtook is refused.
				if ((g.stamped == false) || (clent_LateGroups(g, SNAPGRP_ALL, serverTic) == 0))
				{
					clent_StampGroups(g, SNAPGRP_ALL, serverTic);
					g.leftUnheld = true;
				}
			}
			else if (clent_LateGroups(g, SNAPGRP_ALL, serverTic) != 0)
			{
				// A group stamped after this leave makes it a re-sent copy
				// overtaken by newer records.
				++g_lateLeaves;
				if (pStats != NULL)
					pStats->late++;
			}
			else
			{
				clent_StampGroups(g, SNAPGRP_ALL, serverTic);
				g.present = false;
				// Ids recycle, and the next occupant inherits none.
				g.accelZ = 0.0;
				g.accelPending = 0.0;
				g.haveAccel = false;
				g.haveVelZ = false;
				g.haveEvent = false;
				clent_MarkAbsent(id);
				if (g_leftAtMS != NULL)
					g_leftAtMS[id] = wallMS + 1u;	// biased by 1 so 0 means "never"
			}
			if (pStats != NULL)
				pStats->leaves++;
			continue;
		}

		// A repeat enter is idempotent, applied as a full update and counted.
		if (entering && g.present && (pStats != NULL))
			pStats->redundant++;

		// An absent ghost takes only an enter at or after its leave, anything
		// else read past by its known length.
		if (g.present == false)
		{
			const bool lateHere = g.stamped
				&& (clent_LateGroups(g, SNAPGRP_ALL, serverTic) != 0);
			if ((entering == false) || lateHere)
			{
				if (entering)
				{
					if (off + 2 > size)
					{
						clent_SetError(pszError, errorLen, errors, "truncated classId for id %u", id, 0);
						++errors;
						return errors;
					}
					const bool hasTarget = (Read16(pData + off) & SNAP_CLASS_TARGET) != 0;
					off += 2;
					if (hasTarget)
					{
						if (off + 2 > size)
						{
							clent_SetError(pszError, errorLen, errors, "truncated target for id %u", id, 0);
							++errors;
							return errors;
						}
						off += 2;
					}
				}
				unsigned int skip = 0;
				if (!GroupBytesRead(g_mapFrame, mask, pData + off, size - off, skip))
				{
					clent_SetError(pszError, errorLen, errors,
						"truncated groups for id %u at offset %u", id, off);
					++errors;
					return errors;
				}
				off += skip;

				if (lateHere && (pStats != NULL))
					pStats->late++;
				if (entering && g.leftUnheld)
					++g_lateEntersUnheld;	// by a leave for an id not held
				else if (entering)
					++g_lateEnters;			// a resent enter overtaken by the leave
				else if (lateHere)
					++g_lateAbsent;			// sent before the leave, arrived after
				else
				{
					// At or after the leave, or for an id never seen: the server
					// believes it present and this client does not.
					++g_absentAfterLeave;
					clent_SetError(pszError, errorLen, errors,
						"update for unknown id %u without an enter flag (mask %u)", id, mask);
					++errors;
				}
				continue;
			}
		}

		SnapEntityState st;
		WORD targetId = 0;

		if (entering)
		{
			if (off + 2 > size)
			{
				clent_SetError(pszError, errorLen, errors, "truncated classId for id %u", id, 0);
				++errors;
				return errors;
			}
			memset(&st, 0, sizeof(st));
			st.id = id;
			st.classId = Read16(pData + off);
			off += 2;
			if (st.classId & SNAP_CLASS_TARGET)
			{
				if (off + 2 > size)
				{
					clent_SetError(pszError, errorLen, errors, "truncated target for id %u", id, 0);
					++errors;
					return errors;
				}
				st.classId &= SNAP_CLASS_MASK;
				targetId = Read16(pData + off);
				off += 2;
			}
		}
		else
		{
			// Dirty: unchanged groups carry forward from the newest state.
			st = g.nextState;
		}

		// The length comes from the bytes in hand, not the mask alone.
		unsigned int need = 0;
		if (!GroupBytesRead(g_mapFrame, mask, pData + off, size - off, need))
		{
			clent_SetError(pszError, errorLen, errors,
				"truncated groups for id %u at offset %u", id, off);
			++errors;
			return errors;
		}
		ReadGroups(pData + off, g_mapFrame, st, mask);
		off += need;

		// The checksum, stashed for after the merge, as it is checked against
		// the state held.
		const bool haveSum = ((mask & SNAPGRP_VELMISC) != 0);
		const BYTE wantSum = haveSum
			? (BYTE)((st.misc & SNAP_MISC_CHECKSUM) >> SNAP_MISC_CHECKSUM_SHIFT)
			: (BYTE)0;

		// Late: each carried group taken only where no newer frame wrote it,
		// into the drawn sample only where the game clock allows.
		if (g.present && (clent_LateGroups(g, SNAPGRP_ALL, serverTic) != 0))
		{
			if (pStats != NULL)
				pStats->late++;
			const BYTE taken = (BYTE)(mask & ~clent_LateGroups(g, mask, serverTic));
			g_stampKept += clent_GroupCount((BYTE)(mask & ~taken));
			if (haveSum)
				++g_sumLate;
			if (taken != 0)
			{
				MergeGroups(g.nextState, g_mapFrame, st, taken);
				clent_StampGroups(g, taken, serverTic);

				SnapGhostSample& h = g.hist[g.head];
				BYTE shown = taken;
				if (gameTic < h.posTic)
					shown &= (BYTE)~(SNAPGRP_POS | SNAPGRP_POSZ);
				if (gameTic < h.appearTic)
					shown &= (BYTE)~SNAPGRP_APPEAR;
				g_stampHeldBack += clent_GroupCount((BYTE)(taken & ~shown));
				if (shown != 0)
					MergeGroups(h.state, g_mapFrame, st, shown);
				// Applied, so the law and the measurement run from it.
				if ((shown & SNAPGRP_VELMISC) && (gameTic > g.velZTic))
				{
					g.velZSample = h.state.qvz;
					g.velZTic = gameTic;
					g.haveVelZ = true;
				}
				if (shown & SNAPGRP_POS)
				{
					h.posMS = wallMS;
					h.posTic = gameTic;
				}
				if (shown & SNAPGRP_APPEAR)
					h.appearTic = gameTic;
				if ((taken & SNAPGRP_VELMISC) && MissileFlagged(g.nextState.misc))
					g.everMissile = true;
			}
			if (pStats != NULL)
				pStats->dirty++;
			continue;
		}

		// Not late: every carried group is taken, an enter defining all.
		clent_StampGroups(g, g.present ? mask : (BYTE)SNAPGRP_ALL, serverTic);

		// The previous occupant's render history clears on `entering`, which
		// a lost leave would skip inside the branch below.
		if (entering)
		{
			g.haveLastR = false;
			g.recsSinceRender = 0;
			// A different shooter is a different missile in a reused slot
			// (a lost leave); a re-sent enter carries the same one.
			if (g.targetNetID != targetId)
			{
				g.targetResolved = false;
				g.targetChanged = true;
			}
			g.targetNetID = targetId;
		}

		// [Kaori-Chani] Past every guard, so this id is one the fold must visit.
		if ((g_touchedIds != NULL) && (g_touchedCount < SNAP_MAXID))
			g_touchedIds[g_touchedCount++] = id;

		if (g.present == false)
		{
			g.present = true;
			clent_MarkPresent(id);
			g.targetResolved = false;

			// A fresh ghost has no history; seed the whole ring with the
			// entering state so the first render is stable.
			if (g_leftAtMS != NULL)
				g_leftAtMS[id] = 0;		// a reused slot has not "just left"
			g.head = 0;
			g.count = 1;
			g.everMissile = MissileFlagged(st.misc);
			g.hist[0].state = st;
			g.hist[0].wallMS = wallMS;
			g.hist[0].gameTic = gameTic;
			g.hist[0].serverTic = serverTic;
			// An enter carries position, set here as this path has no carry.
			g.hist[0].posMS = wallMS;
			g.hist[0].posTic = gameTic;
			g.hist[0].appearTic = gameTic;
			// A fresh entity: nothing to difference against yet, and the
			// previous occupant of this slot is not it.
			g.accelZ = 0.0;
			g.accelPending = 0.0;
			g.haveAccel = false;
			g.velZSample = g.hist[0].state.qvz;
			g.velZTic = gameTic;
			g.haveVelZ = true;
			g.haveEvent = false;
			g.enterMS = wallMS;
			if (pStats != NULL)
				pStats->enters++;

			// The arrival, for consumers on the packet clock.
			if ((g_enteredIds != NULL) && (g_enteredCount < SNAP_MAXID))
				g_enteredIds[g_enteredCount++] = id;

			// The local player's shot arriving, when it first can be drawn.
			if (g_fireArmed && g.everMissile)
			{
				const unsigned int waited = wallMS - g_fireMS;
				if (waited > CLENT_FIRE_MATCH_MS)
				{
					g_fireArmed = false;
					++g_fireUnmatched;
				}
				else
				{
					// Through the helper that defines the position scale.
					const int dx = PosToUnits(st.qx) - g_fireX;
					const int dy = PosToUnits(st.qy) - g_fireY;
					if ((dx * dx + dy * dy)
						<= (CLENT_FIRE_MATCH_UNITS * CLENT_FIRE_MATCH_UNITS))
					{
						const int b = (waited < 35) ? 0 : (waited < 70) ? 1
							: (waited < 105) ? 2 : (waited < 175) ? 3 : 4;
						++g_fireHist[b];
						++g_fireSamples;
						if (waited > g_fireWorstMS)
							g_fireWorstMS = waited;
						g_fireArmed = false;

						// Its next few position records, whose catch-up tells
						// enter latency from the bit's warm-up (9.3).
						g_fireTrackId = id;
						g_fireTrackRecs = 0;
					}
				}
			}
		}
		else
		{
			// Classification churn before the head advances, a detonation's
			// flip to 0 apart from a mid-flight one.
			{
				const WORD wasMisc = g.hist[g.head].state.misc;
				const WORD wasCls = (WORD)(wasMisc
					& (SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING));
				const WORD nowCls = (WORD)(st.misc
					& (SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING));
				// Three states, counted as three: a gravity projectile sets
				// both bits and would otherwise be tallied twice.
				if (IsGravBallistic(st.misc))
					++g_gravUpdates;
				else if (IsBallistic(st.misc))
					++g_ballisticUpdates;
				else if (IsSteering(st.misc))
					++g_steeringUpdates;
				if (wasCls != nowCls)
				{
					if (nowCls == 0)
						++g_classEnded;		// left missile state: expected
					else if (wasCls == 0)
						++g_classBegan;		// entered missile state: expected
					else
						++g_classChanged;	// ballistic <-> steering in flight
						// Expected of a periodic steerer, such as a tracer.
				}
			}

			// Would-be blind flight, on a record carrying position, against
			// the replaced state's posTic in game tics.
			if ((mask & SNAPGRP_POS) != 0)
			{
				const SnapGhostSample & was = g.hist[g.head];
				const int dt = gameTic - was.posTic;

				// Every entity, the bound rejecting only a wrapped tic.
				if ((dt > 0) && (dt < 4096))
				{
					const unsigned int pop = clent_Population(id);
					++g_intervalHist[pop][ComputeTicBucket(dt)];
					++g_intervalTotal[pop];
				}

				if (IsBallistic(was.state.misc) && (dt > 0) && (dt < 256))
				{
					const int bb = ComputeTicBucket(dt);
					if (IsBallistic(st.misc) == false)
					{
						// Turned or reclassified mid-gap, counted apart.
						++g_blind.turned[bb];
					}
					else
					{
						// The shared extrapolator with the acceleration, as the
						// sampler does it.
						double px, py, pz;
						ExtrapBallistic(was.state, (double)dt, px, py, pz,
							IsGravBallistic(was.state.misc) ? g.accelZ : 0.0);
						const double ex = px - (double)DequantPos(st.qx);
						const double ey = py - (double)DequantPos(st.qy);
						const double ez = pz - (double)DequantPos(st.qz);
						const double err = sqrt(ex * ex + ey * ey + ez * ez) / 65536.0;
						++g_blind.samples[bb];
						g_blind.errSum[bb] += err;
						if (err > 64.0)
							++g_blind.over64[bb];
					}
				}
			}

			// How fast the tracked shot moved by record ordinal, in units a
			// tic against posTic, before the head advances (9.4).
			if ((id == g_fireTrackId) && (g_fireTrackRecs < 4)
				&& ((mask & SNAPGRP_POS) != 0))
			{
				const SnapGhostSample& was = g.hist[g.head];
				const int dt = gameTic - was.posTic;
				if ((dt > 0) && (dt < 256))
				{
					const int jdx = PosToUnits(st.qx)
						- PosToUnits(was.state.qx);
					const int jdy = PosToUnits(st.qy)
						- PosToUnits(was.state.qy);
					const double moved = sqrt((double)jdx * (double)jdx
						+ (double)jdy * (double)jdy);
					const double rate = moved / (double)dt;

					g_fireJumpAcc[g_fireTrackRecs] += rate;
					++g_fireJumpN[g_fireTrackRecs];
					if (rate > g_fireJumpWorst[g_fireTrackRecs])
						g_fireJumpWorst[g_fireTrackRecs] = rate;
					if (++g_fireTrackRecs >= 4)
						g_fireTrackId = 0;
				}
			}

			const unsigned int prevHead = g.head;
			g.head = (g.head + 1) & CLENT_GHOST_HISTORY_MASK;
			if (MissileFlagged(st.misc))
				g.everMissile = true;
			g.hist[g.head].state = st;
			g.hist[g.head].wallMS = wallMS;
			g.hist[g.head].serverTic = serverTic;
			g.hist[g.head].gameTic = gameTic;
			// Carried forward unless this record brought position.
			g.hist[g.head].posMS = (mask & SNAPGRP_POS)
				? wallMS : g.hist[prevHead].posMS;
			g.hist[g.head].posTic = (mask & SNAPGRP_POS)
				? gameTic : g.hist[prevHead].posTic;
			g.hist[g.head].appearTic = (mask & SNAPGRP_APPEAR)
				? gameTic : g.hist[prevHead].appearTic;

			// A group not carried keeps the previous sample's, event overlay
			// and all.
			if (entering == false)
			{
				const SnapEntityState& was = g.hist[prevHead].state;
				SnapEntityState& now = g.hist[g.head].state;
				const bool bKeepXY = (mask & SNAPGRP_POS) == 0;
				const bool bKeepZ = (mask & (SNAPGRP_POS | SNAPGRP_POSZ)) == 0;
				const bool bKeepAppear = (mask & SNAPGRP_APPEAR) == 0;
				const bool bPosDiff = (bKeepXY && ((now.qx != was.qx) || (now.qy != was.qy)))
					|| (bKeepZ && (now.qz != was.qz));
				const bool bStateDiff = bKeepAppear
					&& ((now.qstate != was.qstate) || (now.qtics != was.qtics));
				if (bPosDiff || bStateDiff)
				{
					++g_evCarried;
					if (bPosDiff)
						++g_evCarriedPos;
					if (bStateDiff)
						++g_evCarriedState;
				}
				if (bKeepXY)
				{
					now.qx = was.qx;
					now.qy = was.qy;
				}
				if (bKeepZ)
					now.qz = was.qz;
				if (bKeepAppear)
				{
					now.qstate = was.qstate;
					now.qtics = was.qtics;
				}
			}

			// Z acceleration whenever velocity arrives, over game tics.
			if (mask & SNAPGRP_VELMISC)
			{
				const SnapEntityState& vs = g.hist[g.head].state;
				// The refusal counters count the gravity class only.
				const bool bGravPop = IsGravBallistic(vs.misc);
				const int span = gameTic - g.velZTic;
				if (g.haveVelZ && (span > 0))
				{
					const double dvz = (double)DequantVel(vs.qvz)
						- (double)DequantVel(g.velZSample);
					const double a = dvz / (double)span;
					const double mag = (a < 0.0) ? -a : a;

					// A reading past the bound spans two unrelated moments, so
					// it is refused, not clamped.
					if (mag > CLENT_MAX_GRAV_ACCEL)
					{
						// The pending reading goes too, so it cannot pair.
						g.haveAccel = false;
						if (bGravPop) ++g_gravRejected;
					}
					else if (g.haveAccel
						&& (((a - g.accelPending) < CLENT_GRAV_AGREE)
							&& ((g.accelPending - a) < CLENT_GRAV_AGREE)))
					{
						// Two consecutive readings agree: adopt it.
						g.accelZ = a;
						g.accelPending = a;
					}
					else
					{
						// A first or disagreeing reading waits as a candidate.
						g.accelPending = a;
						g.haveAccel = true;
						if (bGravPop) ++g_gravUnconfirmed;
					}
				}
				g.velZSample = vs.qvz;
				g.velZTic = gameTic;
				g.haveVelZ = true;
			}
			if (g.count < CLENT_GHOST_HISTORY)
				++g.count;
			// Saturates rather than wraps: a ghost that is never rendered
			// accumulates these, and 255 already means "a burst".
			if (g.recsSinceRender < 255)
				++g.recsSinceRender;
			if (pStats != NULL)
				pStats->dirty++;
		}

		// The checksum against the merged state, a mismatch with an unrepaired
		// loss behind it counted as unsettled.
		if (haveSum)
		{
			// Against the model: the drawn sample may carry an event overlay,
			// which the server's checksum never covers.
			const bool match = (StateChecksum(st) == wantSum);
			if (gapBehind)
			{
				++g_sumUnsettled;
				if (match == false)
					++g_sumUnsettledFailed;
			}
			else
			{
				++g_sumChecked;
				if (match == false)
				{
					++g_sumFailed;
					if (g_sumFirstBadId == 0)
						g_sumFirstBadId = id;
				}
			}
		}

		// Keep the two-point aliases on the two newest samples, for the debug
		// accessor.
		{
			const unsigned int newest = g.head;
			const unsigned int older = (g.head + CLENT_GHOST_HISTORY - 1) & CLENT_GHOST_HISTORY_MASK;
			// The model, not the drawn sample: an event overlay must not
			// become the base later records are merged onto.
			g.nextState = st;
			g.nextMS = g.hist[newest].wallMS;
			g.nextTic = g.hist[newest].serverTic;
			g.prevState = (g.count > 1) ? g.hist[older].state : g.hist[newest].state;
			g.prevMS = (g.count > 1) ? g.hist[older].wallMS : g.hist[newest].wallMS;
			g.prevTic = (g.count > 1) ? g.hist[older].serverTic : g.hist[newest].serverTic;
		}
	}

	// off == size exactly, or we'd have returned above.
	return errors;
}

//*****************************************************************************
//	SAMPLING

// How far in ms a ballistic missile is carried before it freezes (0: off),
// past the missile band's gap and under a detonation's latency.
CVAR(Int, fua_csnap_missile_extrap, CLENT_MISSILE_EXTRAP_MAX_MS,
	CVAR_ARCHIVE | CVAR_NOSETBYACS)

// Ages from the clock model's time for each record's game tic rather than its
// arrival, which takes the client's frame delay and the network's and
// server's jitter out of them. 0 ages from arrival.
CVAR(Bool, fua_csnap_age_servertime, true, CVAR_ARCHIVE | CVAR_NOSETBYACS)
// With fua_csnap_age_servertime, ages go back to arrival while the model's
// window jitter is past this many ms, where its dates drew worse than arrival
// in the replays. 0 never falls back.
CVAR(Int, fua_csnap_age_maxjitter, 50, CVAR_ARCHIVE | CVAR_NOSETBYACS)

// The extrapvel exit's horizon, about 3.5 tics, past the usual record gap.

// The server's ballistic bridge stays under the client's default cap, a lower
// archived value raised on read.
typedef char clent_assert_missile_cap[
	(CLENT_MISSILE_EXTRAP_MAX_MS * TICRATE >= SNAP_BALLISTIC_MAX_TICS * 1000) ? 1 : -1];

unsigned int zx::netsnapshot::MissileCapMS()
{
	if (fua_csnap_missile_extrap <= 0)
		return 0;
	const unsigned int bridgeMS = (SNAP_BALLISTIC_MAX_TICS * 1000 + TICRATE - 1) / TICRATE;
	const unsigned int capMS = (unsigned int)(int)fua_csnap_missile_extrap;
	return (capMS < bridgeMS) ? bridgeMS : capMS;
}

// Keep the biggest few jumps of the window, largest first.
static void clent_NoteJump(unsigned int id, unsigned int classId, unsigned int units,
	int gapTics, unsigned int posGapMS, unsigned int recs, const char* branch)
{
	if (units <= g_jumpLog[CLENT_JUMPLOG - 1].units)
		return;
	int at = CLENT_JUMPLOG - 1;
	while ((at > 0) && (units > g_jumpLog[at - 1].units))
	{
		g_jumpLog[at] = g_jumpLog[at - 1];
		--at;
	}
	g_jumpLog[at].units = units;
	g_jumpLog[at].id = id;
	g_jumpLog[at].classId = classId;
	g_jumpLog[at].gapTics = gapTics;
	g_jumpLog[at].posGapMS = posGapMS;
	g_jumpLog[at].recs = recs;
	g_jumpLog[at].branch = branch;
}

static bool clent_DateAges(int recTic, int posTic, unsigned int renderMS,
	unsigned int& recAt, unsigned int& posAt);
static const SnapClockModel& clent_ClockModel();

// [Kaori-Chani] A quiet sample measures nothing, so every tally the sampler keeps goes
// through here; SampleGhostQuiet raises the flag around its call.
static bool g_sampleQuiet = false;
static void samp_Tally(unsigned int & c)
{
	if (g_sampleQuiet == false)
		++c;
}

// One sample through snap_sample_compute, then the censuses its exit and
// outcome feed.
static bool clent_SampleInner(unsigned int id, unsigned int renderMS, SnapGhostPose& out)
{
	const SnapGhost& g = g_ghosts[id];
	const SnapEntityState& newest = g.hist[g.head].state;

	SnapSampleGhost sg;
	sg.hist = g.hist;
	sg.head = g.head;
	sg.count = g.count;
	sg.enterMS = g.enterMS;
	sg.eventSeq = g.eventSeq;
	sg.accelZ = g.accelZ;
	sg.velZTic = g.velZTic;
	sg.bOther = (clent_Population(id) == CLENT_POP_OTHER);
	sg.newestAt = g.hist[g.head].wallMS;
	sg.posAt = g.hist[g.head].posMS;
	sg.bDated = clent_DateAges(g.hist[g.head].gameTic, g.hist[g.head].posTic, renderMS,
		sg.newestAt, sg.posAt);

	SnapSampleContext c;
	c.clock = &clent_ClockModel();
	c.bHaveServerTic = g_haveServerTic;
	c.newestServerTic = g_newestServerTic;
	c.newestGameTic = g_newestGameTic;
	c.missileCapMS = MissileCapMS();
	c.lawGravity = g_lawGravity;

	const SnapSampleOut o = ComputeSample(sg, c, renderMS, out);

	// The newest record's age, bucketed, dated or from arrival.
	{
		const double d = (double)out.ageMS;
		const int b = (d < 80.0) ? 0 : (d < 150.0) ? 1 : (d < 300.0) ? 2 : (d < 500.0) ? 3 : 4;
		samp_Tally(g_delayBucket[b]);
	}

	const unsigned int ageMS = out.ageMS;
	switch (o.exit)
	{
	case SNAP_SAMPLE_ABSENT:
		return false;

	case SNAP_SAMPLE_AHEAD:
	case SNAP_SAMPLE_EXTRAP:
		if ((g_sampleQuiet == false) && (ageMS > g_misMaxAgeMS))
			g_misMaxAgeMS = ageMS;
		if ((g_sampleQuiet == false) && (ageMS < g_misMinAgeBit))
			g_misMinAgeBit = ageMS;

		// Age distribution rather than a bare maximum: one outlier and a
		// population sitting just past the cap read the same on a max.
		{
			const int b = (ageMS < 100) ? 0 : (ageMS < 250) ? 1 : (ageMS < 500) ? 2 : (ageMS < 1000) ? 3 : 4;
			samp_Tally(g_misCensus.ageBucket[b]);
		}
		if (o.ahead == SNAP_AHEAD_INTERP)
			samp_Tally(g_misCensus.aheadInterp);
		else if (o.ahead == SNAP_AHEAD_JUMP)
			samp_Tally(g_misCensus.aheadJump);
		else if (o.ahead == SNAP_AHEAD_FIRST)
			samp_Tally(g_misCensus.aheadFirst);
		if (o.exit == SNAP_SAMPLE_AHEAD)
		{
			samp_Tally(g_sampExtrap); samp_Tally(g_sampExtrapBallistic); g_lastBranch = "ahead";
			return true;
		}

		if (o.bCapped)
		{
			samp_Tally(g_misExtrapCapped);	// frozen at the cap: this is a visible halt

			if (g_viewValid)
			{
				const double ddx = (double)PosToUnits(newest.qx) - (double)(g_viewX >> 16);
				const double ddy = (double)PosToUnits(newest.qy) - (double)(g_viewY >> 16);
				if ((ddx * ddx + ddy * ddy)
					<= (double)CLENT_MISSILE_NEAR_UNITS * (double)CLENT_MISSILE_NEAR_UNITS)
					samp_Tally(g_misCensus.cappedNear);
				else
					samp_Tally(g_misCensus.cappedFar);
			}
		}
		else
			samp_Tally(g_misExtrap);

		if (o.law != SNAP_LAW_NONE)
		{
			// The measured acceleration, as the sampler took it before the law.
			const double useAccel = g.accelZ;
			samp_Tally(g_gravExtrap);
			if (useAccel == 0.0)
				samp_Tally(g_gravZeroAccel);
			const double mag = (useAccel < 0.0) ? -useAccel : useAccel;
			if (g_sampleQuiet == false)
			{
				// With the count beside it quiet, a sum that is not would read the mean high.
				g_gravAccelSum += mag;
				if (mag > g_gravAccelWorst)
					g_gravAccelWorst = mag;
			}

			if (o.law == SNAP_LAW_USED)
			{
				samp_Tally(g_lawExtrap);
				if (g.haveAccel && (fabs(g.accelZ + o.lawG) > CLENT_GRAV_AGREE))
					samp_Tally(g_lawDisagree);
			}
			else
				samp_Tally(g_lawNoFn);
		}
		samp_Tally(g_sampExtrap); samp_Tally(g_sampExtrapBallistic); g_lastBranch = "extrap";
		return true;

	case SNAP_SAMPLE_EXTRAPVEL:
		if (o.bVelCapped)
			samp_Tally(g_sampExtrapVelCapped);
		if (o.bVelCell)
			samp_Tally(g_sampExtrapVelCell);
		if (o.bVelCellEdge)
			samp_Tally(g_sampExtrapVelCellEdge);
		if (IsSteering(newest.misc))
			samp_Tally(g_sampExtrapVelMissile);
		samp_Tally(g_sampExtrap); samp_Tally(g_sampExtrapVel); g_lastBranch = "extrapvel";
		return true;

	case SNAP_SAMPLE_HELDNEW:
		samp_Tally(g_sampHeldNew); g_lastBranch = "heldnew";
		return true;

	default:
		break;
	}

	// A flagged missile with a wire velocity held here never earned the
	// ballistic bit.
	if (MissileFlagged(newest.misc) && ((newest.qvx | newest.qvy | newest.qvz) != 0))
	{
		samp_Tally(g_misHeldNoBit);
		if ((g_sampleQuiet == false) && (ageMS < g_misMinAgeNoBit))
			g_misMinAgeNoBit = ageMS;
	}

	if (o.exit == SNAP_SAMPLE_HELDMOVE)
	{
		samp_Tally(g_sampHeldMoving); g_lastBranch = "heldmove";
		if (g_sampleQuiet == false)
			clent_NoteHeldMove(g, id);
	}
	else
		{ samp_Tally(g_sampHeld); g_lastBranch = "held"; }
	return true;
}

// [Kaori-Chani] The sample without the measuring, for a caller that is not the pose the
// mirror lands on: the jump, the span and recsSinceRender describe that one alone.
bool zx::netsnapshot::SampleGhostQuiet(unsigned int id, unsigned int atMS, SnapGhostPose& out)
{
	// The branch is left as the measured sample found it, since SampleExit names that one.
	const char * pszWas = g_lastBranch;
	g_sampleQuiet = true;
	const bool ok = clent_SampleInner(id, atMS, out);
	g_sampleQuiet = false;
	g_lastBranch = pszWas;
	return ok;
}

// Samples, then measures how far the rendered position moved since last frame.
bool zx::netsnapshot::SampleGhost(unsigned int id, unsigned int renderMS, SnapGhostPose& out)
{
	g_lastBranch = "?";
	const bool ok = clent_SampleInner(id, renderMS, out);
	if ((ok == false) || (id >= SNAP_MAXID))
		return ok;

	SnapGhost& g = g_ghosts[id];
	if (g.haveLastR)
	{
		const double dx = ((double)out.x - (double)g.lastRX) / 65536.0;
		const double dy = ((double)out.y - (double)g.lastRY) / 65536.0;
		const double dz = ((double)out.z - (double)g.lastRZ) / 65536.0;
		const double u = sqrt(dx * dx + dy * dy + dz * dz);
		if (u >= 1.0)
		{
			// Over the span since this ghost's previous render, 0 meaning the
			// sampler moved it.
			int gapTics = g.hist[g.head].posTic - g.lastRPosTic;
			unsigned int posGapMS = (g.hist[g.head].posMS > g.lastRPosMS)
				? g.hist[g.head].posMS - g.lastRPosMS : 0;
			if ((gapTics < 0) || (gapTics >= 4096))
				gapTics = 0;

			clent_NoteJump(id, g.hist[g.head].state.classId,
				(unsigned int)u, gapTics, posGapMS, g.recsSinceRender,
				g_lastBranch);

			// Only jumps big enough to be seen.
			if (u >= 64.0)
			{
				const int b = ComputeTicBucket(gapTics);
				const unsigned int pop = clent_Population(id);
				++g_jumpGapHist[pop][b];
				g_jumpGapUnits[pop][b] += (unsigned int)u;
				++g_jumpCounted[pop];
			}
		}
	}
	g.lastRX = out.x;
	g.lastRY = out.y;
	g.lastRZ = out.z;
	g.lastRPosTic = g.hist[g.head].posTic;
	g.lastRPosMS = g.hist[g.head].posMS;
	g.recsSinceRender = 0;
	g.haveLastR = true;
	return ok;
}

const char* zx::netsnapshot::SampleExit()
{
	return g_lastBranch;
}

// Drain the worst rendered jumps of the window, largest first.
void zx::netsnapshot::JumpGapStats(unsigned int counts[][CLENT_INTERVAL_BUCKETS],
	unsigned int units[][CLENT_INTERVAL_BUCKETS], unsigned int* total, bool reset)
{
	for (unsigned int p = 0; p < CLENT_POP_COUNT; ++p)
	{
		for (unsigned int i = 0; i < CLENT_INTERVAL_BUCKETS; ++i)
		{
			counts[p][i] = g_jumpGapHist[p][i];
			units[p][i] = g_jumpGapUnits[p][i];
		}
		total[p] = g_jumpCounted[p];
	}
	if (reset)
	{
		memset(g_jumpGapHist, 0, sizeof(g_jumpGapHist));
		memset(g_jumpGapUnits, 0, sizeof(g_jumpGapUnits));
		memset(g_jumpCounted, 0, sizeof(g_jumpCounted));
	}
}

void zx::netsnapshot::IntervalStats(unsigned int hist[][CLENT_INTERVAL_BUCKETS],
	unsigned int* posTotal, unsigned int* recTotal, bool reset)
{
	for (unsigned int p = 0; p < CLENT_POP_COUNT; ++p)
	{
		for (unsigned int i = 0; i < CLENT_INTERVAL_BUCKETS; ++i)
			hist[p][i] = g_intervalHist[p][i];
		posTotal[p] = g_intervalTotal[p];
		recTotal[p] = g_recTotal[p];
	}
	if (reset)
	{
		memset(g_intervalHist, 0, sizeof(g_intervalHist));
		memset(g_intervalTotal, 0, sizeof(g_intervalTotal));
		memset(g_recTotal, 0, sizeof(g_recTotal));
	}
}

unsigned int zx::netsnapshot::TakeJumpLog(SnapJumpInfo* pOut, unsigned int maxOut)
{
	unsigned int n = 0;
	for (unsigned int i = 0; (i < CLENT_JUMPLOG) && (n < maxOut); ++i)
	{
		if (g_jumpLog[i].units == 0)
			break;
		pOut[n].units = g_jumpLog[i].units;
		pOut[n].id = g_jumpLog[i].id;
		pOut[n].classId = g_jumpLog[i].classId;
		pOut[n].gapTics = g_jumpLog[i].gapTics;
		pOut[n].posGapMS = g_jumpLog[i].posGapMS;
		pOut[n].recs = g_jumpLog[i].recs;
		pOut[n].branch = g_jumpLog[i].branch;
		++n;
	}
	memset(g_jumpLog, 0, sizeof(g_jumpLog));
	return n;
}

//*****************************************************************************
//	CONSUMER ACCESSORS

// Ids that entered on the most recent applied delta.
const unsigned int* zx::netsnapshot::EnteredIds(unsigned int& count)
{
	count = g_enteredCount;
	return g_enteredIds;
}

// The sampler exit census, cleared on read when asked.
void zx::netsnapshot::SampleCensus(SnapSampleCensus& out, bool reset)
{
	out.held = g_sampHeld;
	out.heldNew = g_sampHeldNew;
	out.heldMoving = g_sampHeldMoving;
	out.extrapolated = g_sampExtrap;
	out.extrapVel = g_sampExtrapVel;
	out.extrapVelCapped = g_sampExtrapVelCapped;
	out.extrapVelMissile = g_sampExtrapVelMissile;
	out.extrapVelCell = g_sampExtrapVelCell;
	out.extrapVelCellEdge = g_sampExtrapVelCellEdge;
	out.jumped = g_sampJump;
	for (int b = 0; b < 5; ++b)
		out.delayBucket[b] = g_delayBucket[b];

	out.ballisticUpdates = g_ballisticUpdates;
	out.steeringUpdates = g_steeringUpdates;
	out.classEnded = g_classEnded;
	out.classBegan = g_classBegan;
	out.classChanged = g_classChanged;

	if (reset)
	{
		g_sampHeld = g_sampHeldNew = g_sampHeldMoving = 0;
		g_sampExtrap = g_sampJump = 0;
		g_sampExtrapBallistic = 0;
		g_sampExtrapVel = g_sampExtrapVelCapped = g_sampExtrapVelMissile = 0;
		g_sampExtrapVelCell = g_sampExtrapVelCellEdge = 0;
		memset(g_delayBucket, 0, sizeof(g_delayBucket));
		g_ballisticUpdates = g_steeringUpdates = 0;
		g_classEnded = g_classBegan = g_classChanged = 0;
	}
}

//*****************************************************************************
//	SERVER CLOCK MODEL (design doc section 8)
//
// A fit of server tic to arrival time that tracks the server's actual tic
// period (snap_clock_compute), with report counters that outlive a reset.

static SnapClockModel	g_clock;

// The model, for the sampler's dated-ahead pair.
static const SnapClockModel& clent_ClockModel()
{
	return g_clock;
}

static double		g_clkWorstResid = 0.0;
static double		g_clkResidSum = 0.0;
static unsigned int	g_clkResidCount = 0;
static double		g_clkReadyMin = 0.0;
static double		g_clkReadyMax = 0.0;
// Ages dated per sample, as SnapClockStats describes them.
static unsigned int	g_ageByModel = 0, g_ageUntrusted = 0, g_ageJittery = 0;
static unsigned int	g_ageSwitches = 0, g_ageAhead = 0, g_ageOffsetPast50 = 0;
static double		g_ageOffsetSum = 0.0;
// The last sample's source once trusted, for the switch count; a reset clears it.
static int			g_ageLastSource = SNAP_AGE_UNTRUSTED;
static unsigned int	g_clkRejected = 0;
static unsigned int	g_clkResets = 0;

void zx::netsnapshot::ClockReset()
{
	ComputeClockReset(g_clock);
	g_ageLastSource = SNAP_AGE_UNTRUSTED;
}

void zx::netsnapshot::ClockSample(int serverTic, unsigned int wallMS)
{
	const SnapClockSampled s = ComputeClockSample(g_clock, serverTic, wallMS);
	if ((s.outcome == SNAP_CLOCK_RESET) || (s.outcome == SNAP_CLOCK_RESEEDED))
	{
		++g_clkResets;
		g_ageLastSource = SNAP_AGE_UNTRUSTED;
	}
	if ((s.outcome == SNAP_CLOCK_REJECTED) || (s.outcome == SNAP_CLOCK_RESEEDED))
		++g_clkRejected;
	if (s.bResidual)
	{
		if (s.residual > g_clkWorstResid)
			g_clkWorstResid = s.residual;
		g_clkResidSum += s.residual;
		++g_clkResidCount;
	}
	if ((s.outcome == SNAP_CLOCK_ACCEPTED) && ComputeClockReady(g_clock))
	{
		if ((g_clkReadyMin == 0.0) || (g_clock.slope < g_clkReadyMin))
			g_clkReadyMin = g_clock.slope;
		if (g_clock.slope > g_clkReadyMax)
			g_clkReadyMax = g_clock.slope;
	}
}

bool zx::netsnapshot::ClockReady()
{
	return ComputeClockReady(g_clock);
}

double zx::netsnapshot::ClockMsPerTic()
{
	return ComputeClockMsPerTic(g_clock);
}

// From every applied delta, a frame's first datagram only: its later chunks
// arrive after the frame did, and a reordered one's tic going back would reset
// the model.
static void clent_ClockFeed(int gameTic, unsigned int wallMS)
{
	if (ComputeClockFeeds(g_clock, gameTic))
		ClockSample(gameTic, wallMS);
}

static double clent_ClockDateTic(int tic)
{
	return ComputeClockDate(g_clock.baseMS, g_clock.intercept, g_clock.slope, g_clock.baseTic, tic);
}

bool zx::netsnapshot::ClockTicAt(unsigned int ms, double& tic)
{
	return ComputeClockTicAt(g_clock, ms, tic);
}

// The origins of a sample's two ages, arrival times unless the model dates
// them, which it returns true for.
static bool clent_DateAges(int recTic, int posTic, unsigned int renderMS,
	unsigned int& recAt, unsigned int& posAt)
{
	if (fua_csnap_age_servertime == false)
		return false;
	const bool bTrusted = ComputeClockTrusted(g_clock);
	const int source = ComputeAgeSource(bTrusted, g_clock.windowResid, fua_csnap_age_maxjitter);
	if (source != SNAP_AGE_UNTRUSTED)
	{
		if ((g_ageLastSource != SNAP_AGE_UNTRUSTED) && (source != g_ageLastSource))
			++g_ageSwitches;
		g_ageLastSource = source;
	}
	if (source == SNAP_AGE_UNTRUSTED)
		++g_ageUntrusted;
	if (source == SNAP_AGE_JITTERY)
		++g_ageJittery;
	if (source != SNAP_AGE_MODEL)
		return false;

	const double rec = clent_ClockDateTic(recTic);
	const double pos = clent_ClockDateTic(posTic);
	const double offset = fabs(rec - (double)recAt);
	++g_ageByModel;
	g_ageOffsetSum += offset;
	if (offset > 50.0)
		++g_ageOffsetPast50;
	if (rec > (double)renderMS)
		++g_ageAhead;
	recAt = (rec > 0.0) ? (unsigned int)rec : 0;
	posAt = (pos > 0.0) ? (unsigned int)pos : 0;
	return true;
}

void zx::netsnapshot::ClockStats(SnapClockStats& out, bool reset)
{
	out.ready = ClockReady();
	out.msPerTic = ClockMsPerTic();
	out.samples = g_clock.count;
	out.jitterMS = (g_clkResidCount > 0) ? (g_clkResidSum / g_clkResidCount) : 0.0;
	out.recentMS = g_clock.meanAbsResid;
	out.worstMS = g_clkWorstResid;
	out.rejected = g_clkRejected;
	out.resets = g_clkResets;
	out.readyMin = g_clkReadyMin;
	out.readyMax = g_clkReadyMax;
	out.windowMS = g_clock.windowResid;
	out.byModel = g_ageByModel;
	out.untrusted = g_ageUntrusted;
	out.jittery = g_ageJittery;
	out.switches = g_ageSwitches;
	out.ahead = g_ageAhead;
	out.offsetMS = (g_ageByModel > 0) ? (g_ageOffsetSum / g_ageByModel) : 0.0;
	out.offsetPast50 = g_ageOffsetPast50;
	if (reset)
	{
		g_ageByModel = g_ageUntrusted = g_ageJittery = 0;
		g_ageSwitches = g_ageAhead = g_ageOffsetPast50 = 0;
		g_ageOffsetSum = 0.0;
		g_clkReadyMin = g_clkReadyMax = 0.0;
		g_clkWorstResid = g_clkResidSum = 0.0;
		g_clkResidCount = 0;
		g_clkRejected = g_clkResets = 0;
	}
}

// A converging model's estimate is not printed: before 32 samples, a burst
// of frames after a map load can make it read 0.05 ms/tic.
void zx::netsnapshot::ClockDescribe(const SnapClockStats& ck, char* pszOut, size_t len)
{
	char now[40], range[48];
	if (ck.ready)
		mysnprintf(now, sizeof(now), "%.3f ms/tic", ck.msPerTic);
	else
		mysnprintf(now, sizeof(now), "converging");
	range[0] = '\0';
	if (ck.readyMax > 0.0)
		mysnprintf(range, sizeof(range), ", %.3f-%.3f this interval", ck.readyMin, ck.readyMax);
	char aged[192];
	aged[0] = '\0';
	if ((ck.byModel | ck.untrusted | ck.jittery) != 0)
	{
		const double n = (ck.byModel > 0) ? (double)ck.byModel : 1.0;
		mysnprintf(aged, sizeof(aged), " | aged %u by the model (%u ahead), %u untrusted,"
			" %u jittery (window %.1f ms now), %u switches | date offset %.1f ms mean,"
			" %.0f%% past 50", ck.byModel, ck.ahead, ck.untrusted, ck.jittery, ck.windowMS,
			ck.switches, ck.offsetMS, 100.0 * ck.offsetPast50 / n);
	}
	mysnprintf(pszOut, len, "%s (%u samples%s) | jitter %.1f ms mean, %.1f recent, %.1f worst"
		" | %u rejected, %u resets%s", now, ck.samples, range, ck.jitterMS, ck.recentMS,
		ck.worstMS, ck.rejected, ck.resets, aged);
}

//*****************************************************************************
//	SELF-TESTS: the client position codec, and the ghost table and sampler.

unsigned int zx::netsnapshot::RunSelfTests()
{
	unsigned int failures = 0;
	clent_Init();

	SelfTestBlock("client position codec");
	// ---- position codec: 16-bit round trip, and the domain is TOTAL ----
	//
	// A unit a step spans the narrow frame, the top value clamped.
	{
		const fixed_t vals[] =
		{
			0, FRACUNIT, -FRACUNIT, 12345 * FRACUNIT, -32000 * FRACUNIT,
			// [Kaori-Chani] The old 32-bit domain's ends as raw values: FromSignedBits, as
			// Fixed(unsigned) is deleted, sign-extends the second as the old cast did.
			fixed_t::FromSignedBits(0x7FFFFFFFu), fixed_t::FromSignedBits(0x80000000u)
		};
		for (unsigned int i = 0; i < sizeof(vals) / sizeof(vals[0]); ++i)
		{
			BYTE b[2];
			const SnapFrame narrow = FrameNarrow();
			const SDWORD q = QuantPos(vals[i]);
			PutPosIn(b, narrow, 0, q);
			if (ReadPosIn(b, narrow, 0) != q)
			{
				Printf("fua_snap_selftest [client]: position round trip failed for %d\n", (int)q);
				++failures;
			}
			// At most half a unit off, but a full unit at 32767's clamp.
			const fixed_t rt = DequantPos(q);
			const fixed_t res = vals[i] - rt;
			const bool guarded = (vals[i] > (fixed_t)0x7FFF7FFF);
			if (guarded ? ((res < 0) || (res >= FRACUNIT))
				: ((res <= -(FRACUNIT / 2)) || (res > FRACUNIT / 2)))
			{
				Printf("fua_snap_selftest [client]: position residual out of range\n");
				++failures;
			}
		}
		// [Kaori-Chani] Raw ends again: FromSignedBits rather than a cast, as above.
		if ((QuantPos(fixed_t::FromSignedBits(0x7FFFFFFFu)) != 32767)
			|| (QuantPos(fixed_t::FromSignedBits(0x80000000u)) != -32768))
		{
			Printf("fua_snap_selftest [client]: position quantizer does not span fixed_t\n");
			++failures;
		}
	}

	// ---- sampler invariants on a synthetic ghost ----
	// Refused against a live table, which the fixtures' reset would destroy.
	const bool bLiveTable = (PresentCount() > 0);
	if (bLiveTable)
	{
		Printf("fua_snap_selftest: SKIPPED the ghost table block - the table is live "
			"(%u entities). It resets the table, which would blank this client "
			"permanently. Run it before connecting.\n", PresentCount());
	}
	else
	{
		// Named INSIDE the branch that runs it: a block skipped at runtime is
		// then absent from the summary without the summary knowing why.
		SelfTestBlock("ghost table");
		ClientReset();
		SetStreamVersion(ZSNAP_VERSION);
		// Enter at (0,0,0) at 1000 ms, then x +16 at 1060 ms with vx 8 a tic,
		// written by WriteRecord, never by hand.
		BYTE payload[SNAP_MAX_RECORD_BYTES];
		unsigned int n = 0;
		SnapEntityState st;
		// Each fixture is its own payload, so the id base resets each time.
		SnapIdCursor one;

		// Enter id 7, class 1, every group with SNAPGRP_ENTER, fields zero.
		memset(&st, 0, sizeof(st));
		IdCursorReset(one);
		n = WriteRecord(payload, g_mapFrame, 7, one, (BYTE)(SNAPGRP_ALL | SNAPGRP_ENTER), 1, st);
		if (ApplyDelta(payload, n, 100, 100, 1000, NULL, 0, NULL) != 0)
			{ Printf("selftest: fixture enter rejected (id 7)\n"); ++failures; }

		// Dirty id 7 with POS and VEL: x 16 units, vx 8 a tic, a walker's
		// pace on whole units.
		memset(&st, 0, sizeof(st));
		const SWORD qx = 16;					// 16 map units, one unit per step
		st.qx = qx;
		st.qvx = 8 * 4;							// 8 units/tic at 1/4-unit precision
		IdCursorReset(one);
		n = WriteRecord(payload, g_mapFrame, 7, one, (BYTE)(SNAPGRP_POS | SNAPGRP_VELMISC), 0, st);
		if (ApplyDelta(payload, n, 102, 102, 1060, NULL, 0, NULL) != 0)
			{ Printf("selftest: fixture dirty rejected (id 7)\n"); ++failures; }

		SnapGhostPose pose;
		const fixed_t x1 = DequantPos(qx);

		// The sampler is newest state plus extrapolation, so these assert
		// presence, extrapolation of a moving entity, and the cap.

		// A sample at the newest state's own time is that state exactly.
		if (!SampleGhost(7, 1060, pose) || pose.x != x1)
			{ Printf("selftest: newest state\n"); ++failures; }

		// Before the entity existed: not drawn at all.
		if (SampleGhost(7, 500, pose))
			{ Printf("selftest: not-yet-due\n"); ++failures; }

		// Past the newest sample an entity with no wire velocity holds there.
		if (!SampleGhost(7, 2000, pose) || pose.x != x1)
			{ Printf("selftest: held-after\n"); ++failures; }

		// Ballistic extrapolation on the record's own velocity, out to the
		// cap, on a separate ghost.
		{
			// Pinned, since the sample times below are chosen against it.
			const int savedCap = fua_csnap_missile_extrap;
			fua_csnap_missile_extrap = CLENT_MISSILE_EXTRAP_MAX_MS;

			ClientReset();
			SetStreamVersion(ZSNAP_VERSION);
			memset(&st, 0, sizeof(st));
			st.qvx = 8 * 4;						// 8 units/tic
			st.misc = (BYTE)SNAP_MISC_BALLISTIC;	// extrapolate me
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, 9, one, (BYTE)(SNAPGRP_ALL | SNAPGRP_ENTER), 1, st);
			if (ApplyDelta(payload, n, 300, 300, 1000, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture enter rejected (id 9)\n"); ++failures; }

			// POS only, velocity and the bit carrying forward from the enter.
			memset(&st, 0, sizeof(st));
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, 9, one, SNAPGRP_POS, 0, st);
			if (ApplyDelta(payload, n, 302, 302, 1060, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture dirty rejected (id 9)\n"); ++failures; }

			// 50 ms past the newest sample: 1.75 tics * 8 u/t = +14 units.
			if (!SampleGhost(9, 1110, pose)) { ++failures; }
			else
			{
				// [Kaori-Chani] fixed_t times double is deleted, so FRACUNIT is named as a
				// double; the sum and its final cast are unchanged (four more below).
				const fixed_t expect =
					(fixed_t)(8.0 * (double)FRACUNIT * 50.0 * TICRATE / 1000.0);
				if (abs(pose.x - expect) > FRACUNIT / 4)
					{ Printf("selftest: ballistic extrapolation\n"); ++failures; }
			}
			
			// At and well past the cap agree, both relative to the cap.
			SnapGhostPose capA, capB;
			SampleGhost(9, 1060 + CLENT_MISSILE_EXTRAP_MAX_MS, capA);
			SampleGhost(9, 1060 + CLENT_MISSILE_EXTRAP_MAX_MS + 400, capB);
			if (capA.x != capB.x) { Printf("selftest: extrapolation cap\n"); ++failures; }

			fua_csnap_missile_extrap = savedCap;
		}

		// The velocity exit carries a missile-bit ghost to CLENT_VELEXTRAP_MS,
		// and a body is held.
		{
			ClientReset();
			SetStreamVersion(ZSNAP_VERSION);
			const unsigned int bid = MAXPLAYERS + 10;
			memset(&st, 0, sizeof(st));
			st.qx = 16;
			st.qvx = 8 * 4;						// 8 units/tic, no missile bit
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, bid, one, (BYTE)(SNAPGRP_ALL | SNAPGRP_ENTER), 1, st);
			if (ApplyDelta(payload, n, 390, 390, 1000, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture enter rejected (body held)\n"); ++failures; }
			if (!SampleGhost(bid, 1050, pose) || (pose.x != x1) || pose.extrapolated)
				{ Printf("selftest: body with a wire velocity not held\n"); ++failures; }

			ClientReset();
			SetStreamVersion(ZSNAP_VERSION);
			const unsigned int vid = MAXPLAYERS + 6;
			memset(&st, 0, sizeof(st));
			st.qvx = 8 * 4;						// 8 units/tic
			st.misc = SNAP_MISC_STEERING;		// unproven, so extrapvel and not ballistic
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, vid, one, (BYTE)(SNAPGRP_ALL | SNAPGRP_ENTER), 1, st);
			if (ApplyDelta(payload, n, 400, 400, 1000, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture enter rejected (velocity exit)\n"); ++failures; }
			st.qx = qx;
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, vid, one, (BYTE)(SNAPGRP_POS | SNAPGRP_VELMISC), 0, st);
			if (ApplyDelta(payload, n, 402, 402, 1060, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture dirty rejected (velocity exit)\n"); ++failures; }

			// 50 ms is 1.75 tics, +14 units; past the 100 ms cap, 3.5 tics, +28.
			if (!SampleGhost(vid, 1110, pose) || (abs(pose.x - (x1 + 14 * FRACUNIT)) > FRACUNIT / 4))
				{ Printf("selftest: velocity extrapolation\n"); ++failures; }
			if (!SampleGhost(vid, 2000, pose) || (abs(pose.x - (x1 + 28 * FRACUNIT)) > FRACUNIT / 4))
				{ Printf("selftest: velocity extrapolation cap\n"); ++failures; }
			// The wall clamp's path: from the record, over the exit's cap.
			if ((pose.ox != x1) || (pose.exCapMS != 100))
				{ Printf("selftest: velocity extrapolation path\n"); ++failures; }

			// Velocity alone: x runs 50 ms to its cell edge (+0.5) and z 30 ms
			// (-4.2 units), with accelZ 0.
			st.qvx = 4 * 4;
			st.qvz = -4 * 4;
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, vid, one, SNAPGRP_VELMISC, 0, st);
			if (ApplyDelta(payload, n, 403, 403, 1080, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture velocity rejected (velocity exit)\n"); ++failures; }
			if (!SampleGhost(vid, 1110, pose) || (pose.x != x1 + FRACUNIT / 2)
				|| (abs(pose.z + (fixed_t)(4.2 * (double)FRACUNIT)) > FRACUNIT / 4))
				{ Printf("selftest: velocity extrapolation in the x/y cell\n"); ++failures; }
			if (pose.exCapMS != 0)
				{ Printf("selftest: no wall path in the x/y cell\n"); ++failures; }

			// Position alone: the velocity it omits is unchanged, so x/y move
			// again, 4 u/t over the 3.5-tic cap.
			st.qx = 2 * qx;
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, vid, one, SNAPGRP_POS, 0, st);
			if (ApplyDelta(payload, n, 404, 404, 1120, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture position rejected (velocity exit)\n"); ++failures; }
			if (!SampleGhost(vid, 2000, pose)
				|| (abs(pose.x - (DequantPos(2 * qx) + 14 * FRACUNIT)) > FRACUNIT / 4))
				{ Printf("selftest: velocity kept by a position-only record\n"); ++failures; }

			// First record only: carried by its wire velocity, not held at
			// spawn. 50 ms at 25 u/t is +43.75 units.
			ClientReset();
			SetStreamVersion(ZSNAP_VERSION);
			const unsigned int fid = MAXPLAYERS + 9;
			memset(&st, 0, sizeof(st));
			st.qvx = 25 * 4;
			st.misc = SNAP_MISC_STEERING;			// unproven, as a new missile is
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, fid, one, (BYTE)(SNAPGRP_ALL | SNAPGRP_ENTER), 1, st);
			if (ApplyDelta(payload, n, 700, 700, 5000, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture enter rejected (first sample)\n"); ++failures; }
			if (!SampleGhost(fid, 5050, pose)
				|| (abs(pose.x - (fixed_t)(43.75 * (double)FRACUNIT)) > FRACUNIT / 4))
				{ Printf("selftest: first sample not carried by its velocity\n"); ++failures; }

			// Slow, 0.25 u/t: a velocity-only record must not draw x back to
			// the centre of the cell it is crossing.
			ClientReset();
			SetStreamVersion(ZSNAP_VERSION);
			const unsigned int sid = MAXPLAYERS + 8;
			memset(&st, 0, sizeof(st));
			st.qx = 10;
			st.qvx = 1;
			st.misc = SNAP_MISC_STEERING;		// a body would be held, never drawn in its cell
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, sid, one, (BYTE)(SNAPGRP_ALL | SNAPGRP_ENTER), 1, st);
			if (ApplyDelta(payload, n, 600, 600, 3000, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture enter rejected (slow cell)\n"); ++failures; }
			st.qx = 11;
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, sid, one, (BYTE)(SNAPGRP_POS | SNAPGRP_VELMISC), 0, st);
			if (ApplyDelta(payload, n, 601, 601, 3030, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture dirty rejected (slow cell)\n"); ++failures; }
			SnapGhostPose before;
			const bool bBefore = SampleGhost(sid, 3055, before);
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, sid, one, SNAPGRP_VELMISC, 0, st);
			if (ApplyDelta(payload, n, 602, 602, 3060, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture velocity rejected (slow cell)\n"); ++failures; }
			if (!bBefore || !SampleGhost(sid, 3062, pose) || (pose.x < before.x)
				|| (pose.x > DequantPos(11) + FRACUNIT / 2))
				{ Printf("selftest: slow velocity drawn back in its cell\n"); ++failures; }
		}

		// An event a tic old on a missile withheld 13 tics is drawn 1.35
		// tics past its position 10 ms later, not 14.
		{
			const int savedCap = fua_csnap_missile_extrap;
			fua_csnap_missile_extrap = CLENT_MISSILE_EXTRAP_MAX_MS;
			const ClentAnimatesFn savedAnimates = g_animates;
			g_animates = NULL;		// no mirrors here to ask

			ClientReset();
			SetStreamVersion(ZSNAP_VERSION);
			const unsigned int mid = MAXPLAYERS + 7;
			memset(&st, 0, sizeof(st));
			st.qvx = 25 * 4;						// 25 units/tic
			st.misc = (WORD)SNAP_MISC_BALLISTIC;
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, mid, one, (BYTE)(SNAPGRP_ALL | SNAPGRP_ENTER), 1, st);
			if (ApplyDelta(payload, n, 500, 500, 1000, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture enter rejected (event position)\n"); ++failures; }

			// Appearance only, as a withheld missile's records are.
			st.qtics = 7;
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, mid, one, SNAPGRP_APPEAR, 0, st);
			if (ApplyDelta(payload, n, 507, 507, 1200, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture appearance rejected (event position)\n"); ++failures; }

			st.qx = 13 * 25;
			st.qtics = 5;
			IdCursorReset(one);
			n = WriteRecordId(payload, (WORD)mid, one);
			n += WriteEventBody(payload + n, g_mapFrame, SNAPEV_BRANCH, st, 513);
			if (ApplyDelta(payload, n, 514, 514, 1400, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture event rejected (event position)\n"); ++failures; }

			const fixed_t expect = DequantPos(13 * 25)
				+ (fixed_t)(25.0 * (double)FRACUNIT * (1.0 + 10.0 * TICRATE / 1000.0));
			if (!SampleGhost(mid, 1410, pose) || (abs(pose.x - expect) > FRACUNIT))
				{ Printf("selftest: event position age\n"); ++failures; }

			// A newer appearance record, then the event's copy, dropped.
			const BYTE seqBefore = pose.eventSeq;
			st.qtics = 3;
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, mid, one, SNAPGRP_APPEAR, 0, st);
			if (ApplyDelta(payload, n, 516, 516, 1450, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture appearance rejected (event copy)\n"); ++failures; }
			st.qtics = 5;
			IdCursorReset(one);
			n = WriteRecordId(payload, (WORD)mid, one);
			n += WriteEventBody(payload + n, g_mapFrame, SNAPEV_BRANCH, st, 513);
			if (ApplyDelta(payload, n, 517, 517, 1500, NULL, 0, NULL) != 0)
				{ Printf("selftest: fixture copy rejected (event copy)\n"); ++failures; }
			if (!SampleGhost(mid, 1510, pose) || (pose.eventSeq != seqBefore))
				{ Printf("selftest: duplicate event copy applied\n"); ++failures; }

			g_animates = savedAnimates;
			fua_csnap_missile_extrap = savedCap;
		}

		// A leave stamps an id not held: an older enter is refused and counted
		// apart, a newer one applies, and a cleared table takes any frame.
		{
			ClientReset();
			SetStreamVersion(ZSNAP_VERSION);
			LateEntersUnheld(true);
			memset(&st, 0, sizeof(st));
			const BYTE enterMask = (BYTE)(SNAPGRP_ALL | SNAPGRP_ENTER);

			// Id 21, never held: a leave at 500, then enters at 498 and 502.
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, 21, one, 0, 0, st);
			ApplyDelta(payload, n, 500, 500, 2000, NULL, 0, NULL);
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, 21, one, enterMask, 1, st);
			ApplyDelta(payload, n, 498, 498, 2010, NULL, 0, NULL);
			const bool bRefused = (GhostPresent(21) == false)
				&& (LateEntersUnheld(true) == 1);
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, 21, one, enterMask, 1, st);
			ApplyDelta(payload, n, 502, 502, 2030, NULL, 0, NULL);
			if ((bRefused == false) || (GhostPresent(21) == false))
				{ Printf("selftest: leave for an id never held\n"); ++failures; }

			// Id 22: entered at 600 and left at 610, then a leave again at 620,
			// which stamps it, so an enter from 615 is refused.
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, 22, one, enterMask, 1, st);
			ApplyDelta(payload, n, 600, 600, 3000, NULL, 0, NULL);
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, 22, one, 0, 0, st);
			ApplyDelta(payload, n, 610, 610, 3010, NULL, 0, NULL);
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, 22, one, 0, 0, st);
			ApplyDelta(payload, n, 620, 620, 3020, NULL, 0, NULL);
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, 22, one, enterMask, 1, st);
			ApplyDelta(payload, n, 615, 615, 3030, NULL, 0, NULL);
			if (GhostPresent(22) || (LateEntersUnheld(true) != 1))
				{ Printf("selftest: leave for an id once held\n"); ++failures; }

			// Id 23 stamped at 50000, then cleared: an enter at 10 applies.
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, 23, one, enterMask, 1, st);
			ApplyDelta(payload, n, 50000, 50000, 4000, NULL, 0, NULL);
			ForgetWorld();
			IdCursorReset(one);
			n = WriteRecord(payload, g_mapFrame, 23, one, enterMask, 1, st);
			ApplyDelta(payload, n, 10, 10, 4010, NULL, 0, NULL);
			if (GhostPresent(23) == false)
				{ Printf("selftest: stamps outliving a cleared table\n"); ++failures; }
		}
	}

	// Cleared only if this run built fixtures in it.
	if (bLiveTable == false)
		ClientReset();
	// The decoder blocks fed the clock through ApplyDelta, and its report
	// counters outlive a reset, so they are drained here.
	SnapClockStats drained;
	ClockStats(drained, true);
	return failures;
}
