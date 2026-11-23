//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: entities.h
//
// Description: The client entity layer: snapshot decoder, ghost table,
// sampler and server clock model, free of engine types.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_ENTITIES_H
#define ZX_NETSNAPSHOT_ENTITIES_H

// With no engine types, it runs against recorded .zsnap fixtures as it does
// against live traffic (design doc sections 6 and 8).

#include "basictypes.h"
#include "tables.h"
// SnapPSpriteState, for the psprite sink, included rather than left to order.
#include "features/net-snapshot/format.h"
// SnapGhostPose, the sampler's output, and the ghost history constants.
#include "features/net-snapshot/computation/sample_compute.h"

namespace zx { namespace netsnapshot {

// ---- ghost table ----
void ClientReset();

// Branch events: `orphan` for an entity this client was never told of, `stale`
// older than the state they would overwrite, and `notAnimated` for a mirror
// that could never leave the transient state.
void ClientEventStats(unsigned int& applied, unsigned int& orphan, unsigned int& unknown, unsigned int& stale, unsigned int& notAnimated, unsigned int& stalePos, unsigned int& duplicate);
// Applied events by age in tics and by the state duration they describe.
// age needs 5 entries, duration 4.
void EventAgeStats(unsigned int* age, unsigned int* duration, unsigned int& expired);
void ClientEventStatsReset();
// Record pushes that kept an event's position or state the model lacks.
void EventCarryStats(unsigned int& carried, unsigned int& pos, unsigned int& state);

// Why missiles were drawn where they were: stepped (no ballistic bit),
// extrapolating, or frozen at the cap; minBit and minNoBit are 0xFFFFFFFF when
// none was seen, and sampBallistic is the sampler's ballistic exit.
void MissileStats(unsigned int& heldNoBit, unsigned int& extrap,
unsigned int& capped, unsigned int& maxAgeMS,
unsigned int& minBit, unsigned int& minNoBit,
unsigned int& sampBallistic);
void MissileStatsReset();

}} // namespace zx::netsnapshot

// Would-be blind flight: how far extrapolating from the previous record would
// have strayed, by tics flown blind, which sizes the cap (entities.md).
#define CLENT_BLIND_BUCKETS	5		// <4t, 4-8t, 9-17t, 18-35t, 36t+
struct SnapBlindPredict
{
	unsigned int	samples[CLENT_BLIND_BUCKETS];
	double			errSum[CLENT_BLIND_BUCKETS];	// map units, summed
	unsigned int	over64[CLENT_BLIND_BUCKETS];	// the jump census's visible threshold
	unsigned int	turned[CLENT_BLIND_BUCKETS];
};

namespace zx { namespace netsnapshot {

void BlindPredict(SnapBlindPredict & out, bool reset);

}} // namespace zx::netsnapshot

// The census's near distance for a missile, fua_snap_missile_dist's default,
// since the server's setting is not replicated.
#define CLENT_MISSILE_NEAR_UNITS	4096

// Where ballistic staleness comes from, unreached and detonated apart.

struct SnapMissileCensus
{
	unsigned int ageBucket[5];	// ballistic samples: <100, <250, <500, <1000, 1000+ ms
	unsigned int cappedNear;	// past the cap, inside CLENT_MISSILE_NEAR_UNITS
	unsigned int cappedFar;		// past the cap, beyond it
	// Dated after now by the clock model: drawn between two records, or
	// held for want of an earlier position or across a jump.
	unsigned int aheadInterp, aheadFirst, aheadJump;
};

namespace zx { namespace netsnapshot {

void MissileCensus(SnapMissileCensus & out, bool reset);

// The camera position for the census above, set once per frame.
void SetViewPoint(fixed_t x, fixed_t y);

// Like ClientReset but never allocating the table, for shared paths that run
// where no table exists.
void ForgetWorld();

// Whether an SVC2_SNAPSHOT's map stamp is the joined map's, the caller dropping
// anything else unapplied and unacked.
bool AcceptMapStamp(BYTE stamp);
// The same test without its counters, for a command that is not a snapshot.
bool MapStampCurrent(BYTE stamp);
// Whether this connection has had a SnapJoin, kept across map changes and cleared only at a
// connect, so a previous connection's datagram is told apart from this one's.
bool ConnectionJoined();
void ResetMapStamp();
// The current map's stamp from SnapJoin: joins the map.
void AnchorMapStamp(BYTE stamp);
// The SnapJoins applied so far, each starting a new stream.
unsigned int JoinCount();

// The joined map's position frame from SnapJoin, narrow at origin 0 until then;
// UseMapFrame sets it from a recording or a test.
void SetMapFrame(int originX, int originY, int originZ, int widthX, int widthY,
	int widthZ);
void UseMapFrame(const SnapFrame& frame);
const SnapFrame& ClientMapFrame();
// The bytes each axis travels in, which a frame this build cannot read leaves
// at what the server announced, so a body still reads in step.
void ClientFrameWire(BYTE w[3]);

}} // namespace zx::netsnapshot

struct BYTESTREAM_s;

namespace zx { namespace netsnapshot {

// Reads a whole-unit position on axis k in the map's frame, for commands
// carrying one outside the payload.
fixed_t ReadFramePos(BYTESTREAM_s* pByteStream, unsigned int k);
// The joined stamp, read and set without the recorder's hook, for the spawning
// mode of fua_snap_fuzz, which joins offline and puts the state back.
bool JoinedStamp(BYTE& stamp);
void SetJoinedStamp(bool bJoined, BYTE stamp);

// The server's sound-table answer for the joined map, `match` when this
// client's first serverCount names hash the same (6.1).
void SetSoundTable(BYTE stamp, bool match, unsigned int serverCount, unsigned int localCount);
// SNAP_SOUNDIDS_* bits for CLC_SNAPSHOTACK; 0 until this map's table arrived.
BYTE SoundIdsAnswer();
// True when an SVC2_SNAPSOUND id may be played: this map's table matched and
// the id is below the count the hash covered.
bool SoundIdUsable(unsigned int id);
void CountSoundId(bool bPlayed);
// A sound whose stamp names another map, counted and played by nobody.
void CountSoundOtherMap();
unsigned int SoundOtherMap(bool reset);
void SoundIdStats(unsigned int& played, unsigned int& dropped, unsigned int& serverCount,
	unsigned int& localCount, bool& known, bool& match, bool reset);

unsigned int PresentCount();

// Declares the wire version of what is about to feed the decoder, the current
// one by default and after ClientReset.
void SetStreamVersion(unsigned int version);

// Lists present entity ids into pIds, up to maxIds, returning how many.
unsigned int ListPresent(unsigned int* pIds, unsigned int maxIds);

// The same list without the copy, valid until the next apply or reset.
const unsigned int* PresentIds(unsigned int& count);

// The newest GAME tic this client has been told about, for anything stamping
// a record with elapsed world time rather than a delivery count.
int NewestGameTic();

// Writes every present ghost as an enter block, a recording's baseline,
// returning the bytes, with the newest tic seen in `serverTic`.
unsigned int ExportBaseline(BYTE * pOut, unsigned int maxBytes, int& serverTic);

// The present ids and the newest frame applied, for the presence log; false
// before any frame.
bool PresentIds(const unsigned int*& ids, unsigned int& count, int& newestFrame);

// Cheap single-id presence test, for consumers that hold their own id list and
// need to know which entries have gone away.
bool GhostPresent(unsigned int id);

// Whether this id left within the last `withinMS` of arrival time, telling a
// dropped entity from one never held.
bool LeftWithin(unsigned int id, unsigned int withinMS);

// Where a recently departed id last was, for the positional fallback that
// keeps a late sound audible instead of dropping it.
bool DepartedOrigin(unsigned int id, unsigned int withinMS,
	fixed_t& x, fixed_t& y, fixed_t& z);

}} // namespace zx::netsnapshot

// Raw ghost state, to tell an entity not sent from one not shown.
struct SnapGhostDebug
{
	unsigned short	classId;
	unsigned short	spriteframe;
	// [Kaori-Chani] The state the records carried, which is on the wire where spriteframe
	// is not: SNAP_STATE_UNKNOWN for one its own class does not own.
	WORD			qstate;
	unsigned int	prevMS, nextMS;		// arrival stamps of the two buffered states
	int				prevTic, nextTic;	// server frame numbers they came from
	fixed_t			x, y, z;			// newest record's position, dequantized
	// The newest sample's position and game tic, which a branch event moves
	// past the last record's x, y, z.
	fixed_t			posX, posY, posZ;
	int				posTic;
	fixed_t			velX, velY, velZ;	// the sample's velocity, per tic

	BYTE			effects;			// packed AActor::effects as it arrived
	WORD			misc;				// 16 bits, matching the wire
};

namespace zx { namespace netsnapshot {

bool GhostDebug(unsigned int id, SnapGhostDebug& out);

// The newest record's position of a present ghost, not a branch event's,
// leaving x, y, z alone for one not present.
bool GhostPosition(unsigned int id, fixed_t& x, fixed_t& y, fixed_t& z);

// Class id (getActorNetworkIndex) of a present ghost, or 0 if not present.
unsigned short GhostClassId(unsigned int id);

// The shooter NetID the ghost's enter carried (0 none, kept after the leave),
// whether its mirror resolved it, and whether it changed since the last read.
unsigned short GhostTarget(unsigned int id, bool& resolved, bool& changed);
void GhostTargetResolved(unsigned int id);

// How many present ghosts carry a nonzero effects byte.
unsigned int GhostsWithEffects();

}} // namespace zx::netsnapshot

// One payload's record counts, for ApplyDelta's `pStats`.
struct SnapApplyStats
{
	unsigned int enters, dirty, leaves;
	// Records for state already held, expected live and counted for recordings.
	unsigned int redundant;
	// Records older than state already applied, dropped or taken in part live,
	// counted for recordings.
	unsigned int late;
};

namespace zx { namespace netsnapshot {

// Applies one decoded payload, returning its consistency errors (the first's
// text in pszError if given); `serverTic` is the frame number, a unit of
// delivery, and `gameTic` the world's clock, for elapsed time.
unsigned int ApplyDelta(const BYTE* pData, unsigned int size,
	int serverTic, int gameTic, unsigned int wallMS, char* pszError,
	unsigned int errorLen, SnapApplyStats* pStats);

// Samples a ghost's pose at renderMS, on ApplyDelta's wallMS clock, false if
// the id is not present.
bool SampleGhost(unsigned int id, unsigned int renderMS, SnapGhostPose& out);

// [Kaori-Chani] The same sample, without the measuring SampleGhost does: the rendered
// jump and the record span belong to the one pose a mirror is set to.
bool SampleGhostQuiet(unsigned int id, unsigned int atMS, SnapGhostPose& out);

// The exit the last SampleGhost call took (ahead, extrap, extrapvel,
// heldnew, heldmove, held), or "?" when it drew nothing, for diagnostics.
const char* SampleExit();

// Ids that entered on the latest ApplyDelta, valid until the next, for
// acting on arrivals on the packet clock.
const unsigned int* EnteredIds(unsigned int& count);

// The local player fired from (x, y) at nowMS, paired with the first missile
// entering near there to time the shot's first draw (9.3).
void NoteLocalFire(int x, int y, unsigned int nowMS);

}} // namespace zx::netsnapshot

// The fire-to-draw census, reset on read: `hist` of CLENT_FIRE_BUCKETS, and the
// jump arrays 4 each, one per early record, in units a tic.
#define CLENT_FIRE_BUCKETS       5       // <35, <70, <105, <175, 175+ ms

namespace zx { namespace netsnapshot {

void LocalFireStats(unsigned int* hist, unsigned int& samples,
	unsigned int& worstMS, unsigned int& unmatched,
	double* jumpAcc, unsigned int* jumpN, double* jumpWorst);

}} // namespace zx::netsnapshot

// Which exit the sampler took, per sample (entities.md, Sampling).

struct SnapSampleCensus
{
	unsigned int held;
	unsigned int heldNew;
	unsigned int heldMoving;
	unsigned int extrapolated;
	// Of extrapolated: the short-horizon velocity exit, those held at its cap,
	// and those a missile not yet proven straight.
	unsigned int extrapVel, extrapVelCapped, extrapVelMissile;
	// Of extrapVel: x/y absent from the newest record, held to their rounding
	// cell, and those the clamp stopped at the cell's edge.
	unsigned int extrapVelCell, extrapVelCellEdge;
	unsigned int jumped;

	// The age of each sampled ghost's newest record, dated as ageMS is, in
	// buckets <80, <150, <300, <500 and 500+ ms.
	unsigned int delayBucket[5];

	// The missile bits as they arrive, `classChanged` expected above 0 as a
	// periodic steerer flips.
	unsigned int ballisticUpdates;	// updates arriving with bit 5 set
	unsigned int steeringUpdates;	// updates arriving with bit 6 set
	unsigned int classEnded;		// left missile state (detonation) - expected
	unsigned int classBegan;		// entered missile state - expected
	unsigned int classChanged;		// ballistic <-> steering IN FLIGHT - the real signal
};

namespace zx { namespace netsnapshot {

void SampleCensus(SnapSampleCensus& out, bool reset);

}} // namespace zx::netsnapshot

// What `heldMoving` holds, by wire velocity and class, players apart.
enum
{
	CLENT_HM_VEL,			// wire velocity past CLENT_VELEXTRAP_MS
	CLENT_HM_POSONLY,		// zero wire velocity: warped, stepped or walking
	CLENT_HM_UNPROVEN,		// a missile not yet proven straight (STEERING alone)
	CLENT_HM_RESTED,		// no position for CLENT_HM_REST_TICS: stopped, or far
	CLENT_HM_COUNT
};
// Tics after which a heldmove ghost, likely at rest, is counted apart.
#define CLENT_HM_REST_TICS	8
struct SnapHeldMoveClass
{
	unsigned short	classId;
	unsigned int	n[CLENT_HM_COUNT];
};

namespace zx { namespace netsnapshot {

// Fills up to `max` classes and returns how many; samples past the table's
// capacity land in `overflow`.
unsigned int HeldMoveCensus(SnapHeldMoveClass* out, unsigned int max,
	unsigned int overflow[CLENT_HM_COUNT], unsigned int& players, bool reset);

}} // namespace zx::netsnapshot

// Hooks injected into this layer, each setter returning the one it replaces.

// The gravity class's law (LawAt) at a record's position, the measured
// acceleration serving when unset.
typedef bool (*ClentLawGravityFn)(unsigned int classId, fixed_t x, fixed_t y, double & g, bool & bOldZ);

namespace zx { namespace netsnapshot {

ClentLawGravityFn SetLawGravity(ClentLawGravityFn fn);
// Samples drawn by the law, without one installed, and where the measured
// acceleration disagreed with it by over a quarter unit.
void ClientLawStats(unsigned int & extrap, unsigned int & noLaw, unsigned int & disagree, bool reset);

}} // namespace zx::netsnapshot

// Whether this entity's mirror runs local animation, without which a branch
// event would strand it in a transient state.
typedef bool (*ClentAnimatesFn)(unsigned int id);

namespace zx { namespace netsnapshot {

ClentAnimatesFn SetAnimatesLookup(ClentAnimatesFn fn);

}} // namespace zx::netsnapshot

// An entity's properties off its class defaults, for the mirror that draws them.
typedef void (*ClentPropsFn)(unsigned int id, const SnapProps & props);

// A remote player's psprite record (state on the event bit) for player_t.
typedef void (*ClentPSpriteFn)(unsigned int id, const SnapPSpriteState & st);

namespace zx { namespace netsnapshot {

ClentPSpriteFn SetPSpriteSink(ClentPSpriteFn fn);
ClentPropsFn SetPropsSink(ClentPropsFn fn);
// Property events applied, and ones refused for a body that did not hold what its flags
// claimed; for the client's census line.
void PropsEventStats(unsigned int & applied, unsigned int & bad, bool reset);

}} // namespace zx::netsnapshot

// The local player's block for cl_pred.cpp's ring, `gameTic` the frame's (9.2).
typedef void (*ClentLocalFn)(unsigned int id, const SnapLocalState & st,
	int gameTic);

namespace zx { namespace netsnapshot {

// [Kaori-Chani] The ids the latest ApplyDelta applied a record to, which the fold walks
// once per packet; NULL before the first one, and the count is this packet's alone.
const unsigned int* TouchedIds(unsigned int& count);

ClentLocalFn SetLocalSink(ClentLocalFn fn);

}} // namespace zx::netsnapshot

// One sector or polyobject mover, its envelope decoded here and `bodyBytes`
// already checked (`gen` 0 from a pre-v28 stream).
typedef void (*ClentMoverFn)(BYTE cls, BYTE flags, BYTE gen, WORD index,
	const BYTE * pBody, unsigned int bodyBytes);

namespace zx { namespace netsnapshot {

ClentMoverFn SetMoverSink(ClentMoverFn fn);

// The frame number of the payload being applied now, and its stream's epoch,
// bumped wherever frame numbering restarts, for sinks ordering by frame.
int ApplyingServerTic(void);
unsigned int StreamEpoch(void);

// Movers routed, arrived with no sink (must read 0), and unreadable.
void ClientMoverStats(unsigned int & applied, unsigned int & noSink,
	unsigned int & bad, bool reset);

// Local blocks routed to the sink, and those with none (must read 0).
void ClientLocalStats(unsigned int & applied, unsigned int & noSink, bool reset);

// Psprite records routed to the sink, and those with none (must read 0).
void ClientPSpriteStats(unsigned int & applied, unsigned int & noSink, bool reset);

// The gravity class: records naming it, samples extrapolated with a term, and
// those with no term to apply.
void GravStats(unsigned int & updates, unsigned int & extrap,
	unsigned int & zeroAccel, double & meanAccel, double & worstAccel,
	unsigned int & rejected, unsigned int & unconfirmed, bool reset);

}} // namespace zx::netsnapshot

// Populations for the gap and jump censuses, the local player apart.
#define CLENT_POP_OTHER			0
#define CLENT_POP_REMOTE		1
#define CLENT_POP_LOCAL			2
#define CLENT_POP_COUNT			3

// One bucket set for the interval census and the jump histogram, both tics
// since the last position record.
#define CLENT_INTERVAL_BUCKETS	CLENT_BLIND_BUCKETS

// Bucket labels, unbounded so the assertion beside the definition can fail.
extern const char* const CLENT_PopNames[];
extern const char* const CLENT_IntervalNames[];

namespace zx { namespace netsnapshot {

// The NetID the local player's pawn holds, or 0 (every player remote), set by
// the session or a replay.
void SetLocalPlayerId(unsigned int id);

}} // namespace zx::netsnapshot

// The window's largest rendered jumps, `branch` naming the sampler's exit.
struct SnapJumpInfo
{
	unsigned int units;			// map units moved in one frame
	unsigned int id, classId;
	// The span the jump covers, from the previous render of this ghost.
	int gapTics;				// position clock, server frames
	unsigned int posGapMS;		// the same interval in arrival time
	unsigned int recs;			// records applied inside it - >1 means a burst
	const char* branch;
};

namespace zx { namespace netsnapshot {

unsigned int TakeJumpLog(SnapJumpInfo* pOut, unsigned int maxOut);

// Jumps of 64 units or more by position-record gap and population, `total`
// having CLENT_POP_COUNT entries.
void JumpGapStats(unsigned int counts[][CLENT_INTERVAL_BUCKETS],
	unsigned int units[][CLENT_INTERVAL_BUCKETS], unsigned int* total, bool reset);

// The tic gap between an entity's position records per population, `posTotal`
// counting them and `recTotal` every record, in arrays of CLENT_POP_COUNT.
void IntervalStats(unsigned int hist[][CLENT_INTERVAL_BUCKETS],
	unsigned int* posTotal, unsigned int* recTotal, bool reset);

// Records with a checksum and those that disagreed (`failed`), `unsettled` ones
// checked before a loss behind them was repaired and `late` ones not checked.
void ChecksumStats(unsigned int& checked, unsigned int& failed, unsigned int& firstBadId,
	unsigned int& unsettled, unsigned int& unsettledFailed, unsigned int& late, bool reset);

// Whether a loss before the next datagram decoded may be unrepaired
// (ComputeAckSettled), for that one call.
void SetDatagramGapBehind(bool gap);

// Per-group frame stamps: groups a late record left alone, groups taken into
// the model but held off the drawn sample by a branch event, and leaves,
// enters and updates refused for an absent id (late, and at or after its leave).
void StampStats(unsigned int& kept, unsigned int& heldBack, unsigned int& lateLeaves,
	unsigned int& lateEnters, unsigned int& lateAbsent, unsigned int& absentAfterLeave, bool reset);
// Enters refused by a leave stamped on an id this client did not hold.
unsigned int LateEntersUnheld(bool reset);

// Frame arrival order: `older` payloads behind one applied, and `sameNumber`
// chunks past a frame's first.
void FrameOrderStats(unsigned int& older, unsigned int& worst,
	unsigned int& sameNumber, bool reset);
// Datagrams AcceptMapStamp refused as from a previous map, and as from a
// map not yet loaded.
unsigned int MapStampStale(bool reset);
unsigned int MapStampAhead(bool reset);
// Those it refused for arriving before this map's SnapJoin.
unsigned int BeforeJoin(bool reset);

}} // namespace zx::netsnapshot

// fua_csnap_missile_extrap's default: how far past its newest sample a
// ballistic ghost is carried before it freezes.
#define CLENT_MISSILE_EXTRAP_MAX_MS	500

namespace zx { namespace netsnapshot {

// The missile cap the sampler uses: fua_csnap_missile_extrap, raised to the
// server's bridge if set below it; 0 is off.
unsigned int MissileCapMS();

// ---- server clock model (design doc section 8) ----
// Windowed regression of (server gametic -> wall ms) with outlier
// rejection; converges on the server's ACTUAL tic period, time dilation
// under the catch-up clamp included.
void ClockReset();
void ClockSample(int serverTic, unsigned int wallMS);
bool ClockReady();			// enough samples for a stable estimate
double ClockMsPerTic();		// estimated; nominal is 1000.0/TICRATE
// The game tic the model puts at `ms`, once it would date ages; false before.
bool ClockTicAt(unsigned int ms, double& tic);

}} // namespace zx::netsnapshot

// The clock model for the reports, counted since the last reset.
struct SnapClockStats
{
	bool			ready;
	double			msPerTic;
	unsigned int	samples;		// in the window
	double			jitterMS;		// mean residual over the interval: arrival jitter
	double			recentMS;		// the model's running mean, weighted to the newest
	double			worstMS;		// the largest residual accepted
	unsigned int	rejected;		// refused as outliers, past what worstMS can show
	unsigned int	resets;			// discontinuities and re-seeds
	double			readyMin;		// the estimate's range while ready, 0 if never
	double			readyMax;
	// Aging by the model (fua_csnap_age_servertime), counted per sample.
	double			windowMS;		// mean residual over the window: what maxjitter limits
	unsigned int	byModel;
	unsigned int	untrusted;		// by arrival: under CLENT_CLOCK_TRUST_SAMPLES
	unsigned int	jittery;		// by arrival: windowMS past fua_csnap_age_maxjitter
	unsigned int	switches;		// between byModel and jittery
	// Drawn before the newest record's date, so aged 0 (a small share).
	unsigned int	ahead;
	// The date offset: the newest record's date less its arrival, either way,
	// the mean over byModel, and how many were past 50 ms.
	double			offsetMS;
	unsigned int	offsetPast50;
};

namespace zx { namespace netsnapshot {

void ClockStats(SnapClockStats& out, bool reset);
// The stats as the [snap] and replay lines print them.
void ClockDescribe(const SnapClockStats& ck, char* pszOut, size_t len);

}} // namespace zx::netsnapshot

#endif // ZX_NETSNAPSHOT_ENTITIES_H
