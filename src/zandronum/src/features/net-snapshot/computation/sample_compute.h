//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: sample_compute.h
//
// Description: The client's sampler: where a ghost is drawn at a render
// time, from its newest records, extrapolated where the server flagged the
// motion predictable, and the dated-ahead curve between two records. Pure:
// entities.cpp gathers the ghost's inputs and counts what the sampler
// returns.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_SAMPLE_COMPUTE_H__
#define __SNAP_SAMPLE_COMPUTE_H__

#include "basictypes.h"
#include "tables.h"
#include "features/net-snapshot/format.h"
#include "features/net-snapshot/computation/clock_compute.h"

// Recent states each ghost keeps for the sampler's differences, a power of two
// so the index is a mask.
#define CLENT_GHOST_HISTORY		4
#define CLENT_GHOST_HISTORY_MASK	(CLENT_GHOST_HISTORY - 1)

// Settled sampler parameters.
#define CLENT_VELEXTRAP_MS		100		// a wire velocity is carried this far past its position

// Sampled pose of a ghost at a render time: the newest state, plus
// extrapolation where the server flagged the motion predictable.
struct SnapGhostPose
{
	fixed_t		x, y, z;
	// Velocity as the wire last reported it, which effects such as a rocket's
	// trail read off the actor.
	fixed_t		vx, vy, vz;

	angle_t		angle;
	fixed_t		pitch;
	WORD		spriteframe;	// SpriteFromPack / FrameFromPack to split
	DWORD		renderstate;
	// 16 bits, matching the wire: a narrower field drops the high bits after
	// the checksum has verified the record, where nothing can see it.
	WORD		misc;
	BYTE		effects;		// packed; UnpackEffects to apply
	BYTE		tics;			// remaining tics in the wire's state; 255 = unknown
	WORD		stateIdx;		// index into the class's OwnedStates; 0xFFFF unknown
	BYTE		eventSeq;		// bumped per branch event; a change forces a resync
	bool		extrapolated;	// carried past the newest record; the mirror clamps it to the floor
	bool		dated;			// its ages dated by the clock model, not by arrival
	// The extrapolation's origin, Z acceleration and cap, for the wall clamp
	// (exCapMS 0: no wire-velocity path).
	fixed_t		ox, oy, oz;
	double		exAccelZ;
	unsigned int exCapMS;
	// Since the newest sample's time: its date under fua_csnap_age_servertime,
	// else its arrival. arrivalAgeMS is always since its arrival.
	unsigned int ageMS;
	unsigned int arrivalAgeMS;
	unsigned int ageTics;		// server tics since capture; EXACT, no conversion
	// Server tics since qstate and qtics were carried, the phase's age.
	unsigned int appearAgeTics;

	// The tic this state was captured on, for comparing two records.
	int			serverTic;
	// Game tics between the newest position and the one before, at least 1.
	unsigned int posGapTics;
};

// A ghost's ring of recent samples, `head` the newest, with aliases of the two
// newest for the debug accessor and the ingest.
struct SnapGhostSample
{
	SnapEntityState	state;
	unsigned int	wallMS;		// client arrival time
	int				serverTic;
	int				gameTic;	// the record's game tic, which the clock model dates
	// The arrival of the last record that carried position, apart from wallMS,
	// which an appearance-only record advances.
	unsigned int	posMS;
	// The same clock in game tics, for differencing two records.
	int				posTic;
	// The same for appearance in server tics, as old as the last record or
	// event that carried qstate and qtics.
	int				appearTic;
};

// The ghost as the sampler reads it: its history ring, the fields beside it,
// and its ages' origins, which the caller dates.
struct SnapSampleGhost
{
	const SnapGhostSample*	hist;		// CLENT_GHOST_HISTORY entries
	unsigned int	head;				// index of the newest sample
	unsigned int	count;				// valid samples
	unsigned int	enterMS;			// arrival of the first sample
	BYTE			eventSeq;
	double			accelZ;				// measured, units per tic per tic
	int				velZTic;			// the game tic of the velocity the law runs from
	bool			bOther;				// neither the local player nor a remote one
	unsigned int	newestAt;			// the newest record's age origin
	unsigned int	posAt;				// its position's
	bool			bDated;				// both dated by the clock model
};

// What every ghost in a frame shares.
struct SnapSampleContext
{
	const SnapClockModel*	clock;		// dates the ahead pair under bDated
	bool			bHaveServerTic;
	int				newestServerTic;
	int				newestGameTic;
	unsigned int	missileCapMS;		// MissileCapMS, 0 off
	// The gravity law's lookup (SetLawGravity), or NULL.
	bool			(*lawGravity)(unsigned int classId, fixed_t x, fixed_t y, double& g,
						bool& bOldZ);
};

// The exit a sample took.
enum
{
	SNAP_SAMPLE_ABSENT,		// before the ghost entered: not drawn
	SNAP_SAMPLE_AHEAD,		// dated after now, drawn between two records
	SNAP_SAMPLE_EXTRAP,		// ballistic, along its velocity
	SNAP_SAMPLE_EXTRAPVEL,	// a non-player along a fresh wire velocity
	SNAP_SAMPLE_HELDNEW,	// one sample, held
	SNAP_SAMPLE_HELDMOVE,	// held while its last two positions differ
	SNAP_SAMPLE_HELD,		// held at rest
};

// How a dated-ahead attempt ended.
enum
{
	SNAP_AHEAD_NONE,		// not attempted
	SNAP_AHEAD_INTERP,		// drawn between two records
	SNAP_AHEAD_JUMP,		// the pair is too far apart for flight: held
	SNAP_AHEAD_FIRST,		// no position dated at or before now: held
};

// Whether a ballistic gravity sample ran on the law.
enum
{
	SNAP_LAW_NONE,			// not a gravity sample
	SNAP_LAW_USED,			// the law's lookup answered
	SNAP_LAW_NOFN,			// no lookup, or it did not answer: the measured acceleration
};

// What a sample did beyond its pose, for the caller's censuses.
struct SnapSampleOut
{
	int				exit;			// SNAP_SAMPLE_*
	int				ahead;			// SNAP_AHEAD_*
	bool			bCapped;		// a ballistic sample held at the missile cap
	int				law;			// SNAP_LAW_*
	double			lawG;			// the law's gravity, under SNAP_LAW_USED
	bool			bVelCapped;		// the extrapvel exit held at CLENT_VELEXTRAP_MS
	bool			bVelCell;		// it kept x/y to their record's rounding cell
	bool			bVelCellEdge;	// and reached the cell's edge
};

namespace zx { namespace netsnapshot {

// The newest state's pose, unextrapolated.
void ComputePoseFromState(const SnapEntityState& st, SnapGhostPose& out);

// Draws a ballistic ghost dated after now where it was at now, between the
// positions either side, leaving `out` alone unless SNAP_AHEAD_INTERP.
int ComputeAheadPose(const SnapSampleGhost& g, const SnapClockModel& clock,
	unsigned int renderMS, SnapGhostPose& out);

// The ghost's pose at renderMS and the exit that drew it; SNAP_SAMPLE_ABSENT
// sets only the pose's ages and tics.
SnapSampleOut ComputeSample(const SnapSampleGhost& g, const SnapSampleContext& c,
	unsigned int renderMS, SnapGhostPose& out);

// A ballistic ghost whose newest position is dated after now is drawn where it
// was at now: this fraction of the way from an earlier position dated at or
// before now to the next newer one.
double ComputeAheadFraction(double olderDate, double newerDate, double now);

// Cubic Hermite at fraction f between two records `spanTics` apart, with
// positions p0, p1 and velocities v0, v1 a tic.
double ComputeHermite(double p0, double v0, double p1, double v1, double spanTics, double f);

// Whether flight can join two positions `dist` units apart over `spanTics`,
// within twice the faster speed's reach plus 32 units.
bool ComputeFlightJoins(double dist, double speedA, double speedB, double spanTics);

// One definition: the blind-predict and interval censuses bucket the same gaps.
int ComputeTicBucket(int tics);

}} // namespace zx::netsnapshot

#endif // __SNAP_SAMPLE_COMPUTE_H__
