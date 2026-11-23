//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: smooth_compute.h
//
// Description: The mirrors' error smoothing: one frame of the offset that
// carries a ghost from where it was drawn to its new target, which stepped
// ghosts it applies to, which threshold an extrapolated ghost takes by its
// kind, what it reads of a sampled pose, and for a ghost it leaves alone, the
// renderer's slide over the tic. Pure: ghostactor.cpp and csnap_replay.cpp own
// the state and the census.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_SMOOTH_COMPUTE_H__
#define __SNAP_SMOOTH_COMPUTE_H__

#include "basictypes.h"		// fixed_t, BYTE
#include "features/net-snapshot/computation/sample_compute.h"	// SnapGhostPose

// One ghost's smoothing, the drawn position being the target plus an offset
// that decays over the record gap (ghostactor.md).
struct SnapSmooth
{
	// Floats for 65,536 entries of a few units, drawn at target plus
	// offset plus retained.
	float			ox, oy, oz;		// offset, fixed units, decaying over the record gap
	float			speed;			// its decay, fixed units per ms
	// Extrapolated ghosts: the drawn path's place in the record's rounding
	// cell, held while it moves and settled once still.
	float			kx, ky, kz;
	float			kspeed;			// its settle, fixed units per ms
	float			rx, ry, rz;		// the model's own motion per ms, between records
	float			vx, vy, vz;		// last frame's wire velocity per ms, until rx is measured
	// Stepped ghosts: the current segment's box, which interpolation stays in.
	float			bx0, bx1, by0, by1;
	fixed_t			tx, ty, tz;		// last frame's target
	unsigned int	ms;				// last frame's render time
	int				serverTic;
	BYTE			eventSeq;
	bool			extrap;			// last frame's target was extrapolated
	bool			extrapFam;		// smoothed as extrapolated: is, or was and still settling
	bool			haveRate;		// rx measured since the last fold
	bool			valid;
};

// What one frame of smoothing reads of the sampler's pose: the target, after
// the wall clamp, and what says whether a record or event arrived.
struct SnapSmoothIn
{
	fixed_t			x, y, z;
	fixed_t			vx, vy, vz;		// wire velocity
	unsigned int	posGapTics;		// the newest record's tic gap
	int				serverTic;
	BYTE			eventSeq;
	bool			extrapolated;
	// Its kind, SNAP_SMOOTH_*: a missile of either kind takes the missile
	// threshold, and the census splits by all three.
	BYTE			kind;
	// For the census only: ages dated by the clock model rather than by arrival.
	bool			dated;
};

// An extrapolated ghost's kind, body, ballistic or turning, only a ballistic
// missile taking the missile threshold.
#define SNAP_SMOOTH_BODY		0
#define SNAP_SMOOTH_BALLISTIC	1
#define SNAP_SMOOTH_TURNING		2
#define SNAP_SMOOTH_KINDS		3

namespace zx { namespace netsnapshot {

// What smoothing reads of a sampled pose, for the live mirror and the replay
// alike.
SnapSmoothIn ComputeSmoothIn(const SnapGhostPose& pose);

}} // namespace zx::netsnapshot

// The settings: the three thresholds, the stepped population, a map reset in
// progress, and the tic rate.
struct SnapSmoothCfg
{
	int				stepLimit;
	int				extrapLimit;
	int				missileLimit;
	bool			bStepPop;
	bool			bInReset;
	int				ticRate;
};

namespace zx { namespace netsnapshot {

// Whether a stepped ghost is smoothed, by fua_csnap_smooth_entities: 2 or above
// all, 1 remote players (ids 1 to maxPlayers, not localPlayer + 1), else none.
bool ComputeSmoothStepPop(int pop, unsigned int id, int localPlayer,
	int maxPlayers);

}} // namespace zx::netsnapshot

// One cell of the extrapolated census, split by kind and by what
// dated it (ghostactor.md).
struct SnapSmoothSplit
{
	unsigned int	folds, snaps, snapsBack, reversed, moving;
	unsigned int	snapSize[3];
	unsigned int	off15, off45;
	double			foldSum;
};

// The census's window totals, [0] stepped and [1] extrapolated.
struct SnapSmoothStats
{
	unsigned int	folds[2], snaps[2];
	double			foldSum[2];
	unsigned int	resid[2][4];
	unsigned int	offFrames;		// frames drawn off the target
	// A stepped ghost drawn over a unit outside its segment (must read 0).
	unsigned int	past;
	// Records that moved nothing, kept out of the fold mean.
	unsigned int	zero;
	// Frames an extrapolated ghost moved back against its wire velocity, of
	// frames it had one.
	unsigned int	reversed, movingFrames;
	// The extrapolated census again, [kind][dated]: kind SNAP_SMOOTH_*; dated
	// 0 by arrival, 1 by the clock model.
	SnapSmoothSplit	ex[SNAP_SMOOTH_KINDS][2];
};

namespace zx { namespace netsnapshot {

// One frame of smoothing for one ghost, from the sampler's target after the
// wall clamp to the position the floor and ceiling clamps see; angle is not
// smoothed, and at a threshold of 0 the target is drawn with each correction
// counted as a snap.
void ComputeSmooth(SnapSmooth & s, const SnapSmoothIn & pose,
	const SnapSmoothCfg & cfg, unsigned int nowMS, SnapSmoothStats & st,
	fixed_t & outX, fixed_t & outY, fixed_t & outZ);

// One cell of the split census as a report's text (false when empty), `unit`
// naming what a moving count counts.
bool ComputeSmoothCellText(const SnapSmoothStats & st, unsigned int kind,
	unsigned int dated, const char * unit, char * text, unsigned int size);

}} // namespace zx::netsnapshot

// Units a tic of the record gap past which a step is drawn as it lands.
#define SNAP_SLIDE_SNAP_UNITS	32

namespace zx { namespace netsnapshot {

// Whether the renderer, not the mirror, interpolates a ghost this frame, read
// after ComputeSmooth: held, outside the stepped smoothing (its population or
// a threshold of 0), not smoothed as extrapolated, and not a walking step to
// draw raw: bRawWalk names a monster under nomonsterinterpolation, whose steps
// stock's P_Move draws raw only when it walks, so only with no wire velocity.
bool ComputeSmoothSlides(const SnapSmooth & s, const SnapSmoothIn & pose,
	const SnapSmoothCfg & cfg, bool bRawWalk);

// One frame of a mirror's position and PrevX, PrevY, PrevZ: a sliding ghost
// moves on its first frame of a tic with prev where it stood, for the renderer
// to slide, and any other, or a step past SNAP_SLIDE_SNAP_UNITS, lands with
// prev equal to pos.
void ComputeSlideFrame(bool bSlides, bool bNewTic, const fixed_t pose[3],
	unsigned int gapTics, fixed_t pos[3], fixed_t prev[3]);

// [Kaori-Chani] The start pose that makes this frame draw `here`, rounded so a resting mirror
// stays on the end; in a tic's last sixteenth, or at frac 1, it is the end and false comes back.
bool ComputeAnchorStart(const fixed_t end[3], const double here[3], fixed_t frac,
	fixed_t start[3]);

}} // namespace zx::netsnapshot

#endif // __SNAP_SMOOTH_COMPUTE_H__
