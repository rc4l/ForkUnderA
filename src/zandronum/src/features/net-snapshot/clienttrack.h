//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: clienttrack.h
//
// Description: The server's per-client snapshot path: what each client
// holds, the acknowledgement fold and repair, the priority scheduler and the
// delta builder that chunks a frame into datagrams.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_CLIENTTRACK_H
#define ZX_NETSNAPSHOT_CLIENTTRACK_H

// Per-client snapshot state: each client is delta'd against `sent`, and its
// acks fold into `believed` (clienttrack.md, The model).

#include "features/net-snapshot/format.h"
#include "features/net-snapshot/computation/interest_compute.h"	// the bands, priority hints and scheduler constants
#include "tarray.h"

// Pending depth in frames, not entries, at which a client is forced back to a
// baseline (about 1.8 s).
#define SNAPCL_MAX_PENDING		64

// The share of a frame's budget events may take, inside fua_snap_maxbytes
// rather than beside it, since branches scale with world activity.
#define SNAPCL_EVENT_SHARE_PCT	30

namespace zx { namespace netsnapshot {

void ServerInit();

// Forget everything about a client: on connect, disconnect, map change, or
// when the client has fallen so far behind that a fresh baseline is cheaper
// than catching up.
void ResetClient(unsigned int client);

// Every slot at once, for map changes.
void ResetAllClients();
// Every client's mover send state, after the mover table is reset.
void ForgetMovers();

// Whether this client holds the entity yet, which a reliable event can outrun.
bool Believes(unsigned int client, unsigned int id);
// How long this client has believed in an entity, in SNAPSHOT FRAMES; 0
// when it does not. nowFrame must be g_snapFrame: the stamp is the builder's
// frame number, so a gametic subtracts two counters with different origins.
unsigned int BelievedFrames(unsigned int client, unsigned int id, unsigned int nowFrame);

// The client acked datagram `seq` (a datagram, not a frame), `history` holding
// the bits for the 32 before it: folds and repairs.
void OnAck(unsigned int client, unsigned int seq, unsigned int history);

// Optimistic references walked back because their frame was proven lost:
// frames, entity rows, mover rows and property slots.
void RepairStats(unsigned int& frames, unsigned int& entities,
	unsigned int& movers, unsigned int& props, bool reset);

// Psprite records emitted, and client-frames stopped by SNAPCL_PSPRITE_MAX,
// which is sized to read 0.
void ServerPSpriteStats(unsigned int & emitted, unsigned int & capped, bool reset);

}} // namespace zx::netsnapshot

// Local-block emission over all clients: `noId` and `noClient` must read 0, and
// `authoritative` counts the records written with the bit.
struct SnapLocalEmitStats
{
	unsigned int emitted;
	unsigned int authoritative;
	unsigned int noPawn;
	unsigned int spectator;
	unsigned int noClient;
	unsigned int noId;
};

#define SNAPCL_OWNENTER_BUCKETS_REPORT	6

namespace zx { namespace netsnapshot {

// Enter-record queue wait in tics for missiles the receiving client fired, the
// scheduler's share of the fire-to-draw latency.
void OwnEnterStats(unsigned int * hist, unsigned int & samples,
	unsigned int & worst, bool reset);

}} // namespace zx::netsnapshot

// Encoding census over the emitted stream, behind fua_snap_wirecensus.
struct SnapWireCensusOut
{
	QWORD	recs[SNAPCL_TIERS_REPORT];
	QWORD	bytes[SNAPCL_TIERS_REPORT];
	QWORD	appOnly[SNAPCL_TIERS_REPORT];
	QWORD	grpRecs[SNAP_GROUP_COUNT];
	QWORD	grpBytes[SNAP_GROUP_COUNT];
	QWORD	enters;
	QWORD	headerBytes;
	QWORD	idGap[3];			// varint width a sorted-id gap would need
	QWORD	idGapSum, idGapN;
	QWORD	chunkGap[3];		// the same, restarted at every chunk boundary
	QWORD	chunkGapSum, chunkGapN;
	QWORD	chunkFirst;			// chunk-opening records, which pay a raw id
	QWORD	qsAppear, qsUnresolved;
	QWORD	qsNarrow8, qsNarrow7;
};

namespace zx { namespace netsnapshot {

void WireCensus(SnapWireCensusOut & out, bool reset);

void ServerLocalStats(SnapLocalEmitStats & out, bool reset);

}} // namespace zx::netsnapshot

// The cap that `capped` counts, so the report can print the number the code
// used rather than a literal of its own (9.2).
#define SNAPCL_PSPRITE_MAX_REPORT	32

// Mover records one client may be sent in a frame, about 1.1 kB at most.
#define SNAPCL_MOVER_MAX		32

namespace zx { namespace netsnapshot {

// Mover records emitted, and client-frames stopped by the cap, which binding on
// an ordinary map means the dirty test fires every tic.
void ServerMoverStats(unsigned int & emitted, unsigned int & capped,
	unsigned int & emitBlocked, unsigned int & stair, unsigned int * perCls,
	unsigned int & outOfBytes, bool reset);

// Whether this client has acked `seq` for this slot (out of range: yes), the
// gone sweep's release test.
bool MoverAcked(unsigned int client, unsigned int slot, WORD seq);

// Whether this connection has acked anything, which survives a rebaseline.
bool StreamStarted(unsigned int client);

// The ack lag datagram `seq` echoes to this client (ComputeAckLag): how far
// behind it the newest ack processed is, 0 before one since the last forget.
BYTE AckLag(unsigned int client, unsigned int seq);

// Enters lost without proof for ids not believed present: marked, leaves
// sent for them once out of the world, marks cleared by a confirmed enter.
void MaybeStats(unsigned int& marked, unsigned int& leaves,
	unsigned int& clearedEnter, bool reset);
// Frames resolved, those the ack history proves lost, acks whose gap
// exceeded the 32-frame window (where nothing can be concluded), frames
// folded on history evidence rather than as the acked frame, `late`: of the
// confirmed, those an earlier ack had seen missing, and `setAfterLost`: bits a
// later ack set for a datagram already declared lost, which the client's
// too-late drop keeps at 0 bar datagrams declared lost without proof that had
// arrived.
void AckLossStats(unsigned int& seen, unsigned int& lost, unsigned int& wide,
	unsigned int& folded, unsigned int& late, unsigned int& setAfterLost, bool reset);
// Acks ignored for naming a datagram not yet sent, since the last reset.
unsigned int AcksAhead(bool reset);

// The reference check (fua_snap_debug_refcheck), over every client after every
// ack: ids with records in flight, ids at rest and psprites, checked against
// `believed` and the records in flight, and those that differ.
void ReferenceCheckStats(unsigned int& inFlight, unsigned int& inFlightDiffer,
	unsigned int& atRest, unsigned int& atRestDiffer, unsigned int& psprites,
	unsigned int& pspritesDiffer, bool reset);

// Enters sent to an id still believed present because its leave was in
// flight (window total), and leave markers on present ids now (a reading).
void PresenceStats(unsigned int& reenters, unsigned int& onPresent, bool reset);

// Appends a line to snappresence-sv.txt: the frame, the client and every id it
// may hold, present, leaving, `maybe` or entered in a pending datagram.
void PresenceLog(unsigned int client, unsigned int frame);

}} // namespace zx::netsnapshot

// The ceiling on the derived appearance quota, never 0, or the far tier reports
// no samples (10.1).
#define SNAPCL_ANIMREF_MAX		40

namespace zx { namespace netsnapshot {

// Enters emitted with a target NetID, over every client.
void TargetStats(unsigned int& enters, bool reset);

}} // namespace zx::netsnapshot




// Where this client looks, one without a view falling back to pure aging.
struct SnapViewpoint
{
	bool		valid;
	fixed_t		x, y, z;
	angle_t		angle;
};

// One datagram's slice of a payload, cut on records so it decodes alone.
struct SnapChunk
{
	unsigned int	offset;		// into the payload buffer
	unsigned int	length;
	unsigned int	seq;		// its own delivery sequence; see OnAck
};

namespace zx { namespace netsnapshot {

// The gravity class's bridge: records withheld, sent because position or
// velocity left the law, past the bridge, worst withheld position error.
void ServerLawStats(unsigned int& held, unsigned int& sentPos, unsigned int& sentVel,
	unsigned int& expired, unsigned int& worst, bool reset);

// Builds this client's delta against `sent` from the capture `world` (sorted
// by id) into `out`, returning its bytes, a client holding nothing getting a
// baseline. `budgetBytes` caps the payload, `frame` is the scheduling clock,
// `chunks` receives each datagram's slice and sequence (none if nothing was
// built), and `chunkBytes` caps a datagram (0 or past the budget: one chunk).
unsigned int BuildDelta(unsigned int client, const SnapEntityState* world,
	const BYTE* priority, unsigned int worldCount, unsigned int frame,
	unsigned int gameTic,
	TArray<SnapChunk>& chunks, TArray<BYTE>& out, unsigned int budgetBytes,
	unsigned int chunkBytes, const SnapViewpoint& view);

// Diagnostics for the priority pass: how many records went out in each tier
// last frame, and how many dirty entities the budget could not carry.
unsigned int LastTierCount(unsigned int tier);
// Entities ranked into a tier, as against records that tier got to send.
unsigned int LastTierSize(unsigned int tier);
// Reserved-pass accounting: bytes set aside, bytes actually spent, and how
// many bands stopped on their own ceiling versus ran out of entities.
unsigned int LastReserveAllowed();
unsigned int LastReserveSpent();
unsigned int LastReserveCapped();
unsigned int LastReserveDrained();
unsigned int LastDeferred();

// The appearance-refresh quota this frame, what it would have been unclamped
// (0 when not clamped), and the population it was derived over.
void AnimrefQuota(unsigned int& quota, unsigned int& wanted, unsigned int& population);

// The records emitted that were new to the client since last frame, which over
// the contested population gives the per-entity refresh rate.
unsigned int LastFresh();
// Records emitted that carried nothing new (pure ack re-sends), and the group
// bytes those re-sends cost.
unsigned int LastResend();
unsigned int LastResendBytes();
// The records emitted that the starvation guard rescued, a high share meaning
// priority has degenerated toward round-robin.
unsigned int LastForced();
// Dirty records withheld as cosmetic (a distant animation frame), those already
// queued for their bounded refresh included.
unsigned int LastCosmetic();

// Records flagged as a terminal sequence, and how many of those the cosmetic
// exemption would otherwise have withheld.
void TerminalStats(unsigned int& seen, unsigned int& promoted,
unsigned int& capped, unsigned int& emitted, bool reset);

}} // namespace zx::netsnapshot

// Appearance suppression, counted at the decision, by population (players,
// whose transitions are input-driven, apart), in arrays of SNAPCL_APOP_COUNT.
#define SNAPCL_APOP_PLAYER	0	// reserved ids, 1..MAXPLAYERS
#define SNAPCL_APOP_OTHER	1
#define SNAPCL_APOP_COUNT	2

extern const char * const SNAPCL_AppearPopNames[];

namespace zx { namespace netsnapshot {

void AppearStats(unsigned int * held, unsigned int * diverged,
	unsigned int * unwalkable, bool reset);

// Ballistic position suppression: `held` against `sent`, `expired` past what
// the client could bridge, and `worst` the largest error withheld.
void PredictStats(unsigned int& held, unsigned int& sent,
	unsigned int& expired, unsigned int& worst, bool reset);

// Records for entities serviceable again after a hold the starvation guard
// cannot see. hist must have 5 entries: <35, <105, <210, <420, 420+ tics.
void ReentryStats(unsigned int& seen, unsigned int* hist, bool reset);

// Records classified into each band and how many carried POS (arrays of
// SNAPCL_TIERS_REPORT entries).
void BandComposition(unsigned int* recs, unsigned int* pos, bool reset);

// What the contested set owes, by group and by how long it has been waiting.
// byGroup needs SNAP_GROUP_COUNT entries; stale and posStale need 4.
void OwedStats(unsigned int* byGroup, unsigned int* stale,
unsigned int* posStale, unsigned int& total, bool reset);

// Diagnostics.
unsigned int PendingFrames(unsigned int client);
unsigned int BelievedCount(unsigned int client);
unsigned int LastAckedSeq(unsigned int client);

// [Kaori-Chani] The newest datagram sequence this client has been given, so a
// report can take its ack lag with AckLag.
unsigned int NewestSentSeq(unsigned int client);

}} // namespace zx::netsnapshot

// Aggregate client health for the stats block, scaling to any client count.
struct SnapClientHealth
{
	unsigned int	activeClients;
	unsigned int	believedAvg, believedMax;
	double			pendingAvg;
	unsigned int	pendingMax;
	unsigned int	rebaselines;	// forced re-baselines this window
};

namespace zx { namespace netsnapshot {

void AggregateHealth(SnapClientHealth& out);
void ResetWindowStats();

}} // namespace zx::netsnapshot

// The contested set by distance ring (512 to 8192 units) and in front of the
// view, over a window, `buckets` being the rings plus one beyond.
#define SNAPCL_CENSUS_MAXBUCKETS	8
struct SnapVisibilityCensus
{
	int				buckets;
	unsigned int	contested;					// total contested this window
	unsigned int	front;						// of which, in front of view
	unsigned int	byDist[SNAPCL_CENSUS_MAXBUCKETS];
	unsigned int	frontByDist[SNAPCL_CENSUS_MAXBUCKETS];
};

namespace zx { namespace netsnapshot {

void GetCensus(SnapVisibilityCensus& out);

// Recomputes the cached cull cosine from the full cone in degrees, for the
// fua_snap_cull_fov callback.
void UpdateCullFov(int fovDegrees);

// Queues a branch event for one client during the delta build, bTerminal (a
// death) sending first on the terminal copies and spacing.
void QueueEvent(unsigned int client, WORD id, BYTE type,
	const SnapEntityState& st, unsigned int frame, unsigned int gameTic, bool bTerminal);

// queued/sent/retried are transmissions; the two expiry counts are the ones
// that matter. expUnsent means the share was too small - the event never got
// budget at all - and is not visible any other way.
void ServerEventStats(unsigned int& queued, unsigned int& sent, unsigned int& retried,
	unsigned int& expUnsent, unsigned int& expUnacked, unsigned int& droppedFull,
	unsigned int& bytes, unsigned int& supUnsent, unsigned int& supSent);

// What the event emitter used over the window's emits with events queued (one
// client, one frame), `breaks` counting those the share stopped.
void EventEmitStats(unsigned int& emits, QWORD& budgetSum, QWORD& shareSum,
	unsigned int& spentMax, QWORD& queueSum, unsigned int& queueMax, unsigned int& breaks);

// Deaths, apart from the rest of the event queue: a flinch that misses its
// frame is invisible, a death that does is a monster still walking.
void EventTerminals(unsigned int& sent, unsigned int& expUnsent,
	unsigned int& retried, unsigned int& leftUnsent, unsigned int& leftPartial);

void EventSkips(unsigned int& visits, unsigned int& done,
	unsigned int& sameFrame, unsigned int& share);
void ServerEventStatsReset();

}} // namespace zx::netsnapshot

#endif // ZX_NETSNAPSHOT_CLIENTTRACK_H
