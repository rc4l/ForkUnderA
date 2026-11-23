// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: clock_compute.h
//
// Description: The client's model of the server clock: a windowed
// least-squares fit of game tic to arrival time, with outlier rejection and
// step-change recovery, and what dates a ghost's ages. Pure: entities.cpp
// owns the model and the report's counters, which these functions feed
// through what they return.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_CLOCK_COMPUTE_H__
#define __SNAP_CLOCK_COMPUTE_H__

#include "doomdef.h"		// TICRATE

#define CLENT_CLOCK_WINDOW			128
#define CLENT_CLOCK_MIN_READY		32
// Samples before ages are dated by the model, past a map load's burst.
#define CLENT_CLOCK_TRUST_SAMPLES	160
// A genuine discontinuity (server restart, clock step, out-of-order or
// corrupt record) - as opposed to merely variable cadence.
#define CLENT_CLOCK_STEP_MS			5000
// Outlier rejection is adaptive and permissive: a multiple of the window's
// residual spread over this floor (entities.md, The clock model).
#define CLENT_CLOCK_OUTLIER_FLOOR_MS	2000.0
#define CLENT_CLOCK_OUTLIER_SIGMAS	8.0
// If this many consecutive samples are rejected, the stream has genuinely
// changed regime rather than jittered: re-seed instead of starving.
#define CLENT_CLOCK_MAX_SKIP_RUN	12

// The fit and the samples behind it; a new model is a reset one.
struct SnapClockModel
{
	double			tic[CLENT_CLOCK_WINDOW] = {};	// each sample's tics past the base
	double			ms[CLENT_CLOCK_WINDOW] = {};	// each sample's ms past the base
	unsigned int	count = 0;
	unsigned int	head = 0;
	int				baseTic = 0;
	unsigned int	baseMS = 0;
	bool			haveBase = false;
	double			slope = 1000.0 / TICRATE;
	double			intercept = 0.0;
	bool			fitValid = false;
	double			meanAbsResid = 0.0;		// the outlier threshold's running spread
	unsigned int	skipRun = 0;
	int				lastTic = 0;
	unsigned int	lastMS = 0;
	bool			haveLast = false;
	unsigned int	accepted = 0;			// since the last reset or re-seed
	double			windowResid = 0.0;		// mean residual over the window's fit
};

// What a sample did, for the report's counters.
enum
{
	SNAP_CLOCK_ACCEPTED,		// joined the window
	SNAP_CLOCK_REJECTED,		// an outlier, discarded
	SNAP_CLOCK_RESEEDED,		// an outlier ending a run of them, which re-seeded the model
	SNAP_CLOCK_RESET,			// a discontinuity, which re-seeded the model
};

struct SnapClockSampled
{
	int				outcome;		// SNAP_CLOCK_*
	bool			bResidual;		// accepted against a fit, `residual` from it
	double			residual;		// absolute, in ms
};

namespace zx { namespace netsnapshot {

// Forget the fit, keeping the last sample's place.
void ComputeClockReset(SnapClockModel& m);

// Fit the window's line and its mean residual; fewer than two samples, or all
// on one tic, leave the fit as it was.
void ComputeClockRefit(SnapClockModel& m);

// One sample: a tic or arrival time going back, or a gap past
// CLENT_CLOCK_STEP_MS, re-seeds; an outlier is rejected, and a run of them
// re-seeds; anything else joins the window and refits.
SnapClockSampled ComputeClockSample(SnapClockModel& m, int serverTic, unsigned int wallMS);

// Enough samples for a stable estimate.
bool ComputeClockReady(const SnapClockModel& m);

// The fitted ms per tic, or the nominal 1000/TICRATE without a fit.
double ComputeClockMsPerTic(const SnapClockModel& m);

// Whether a frame's tic feeds the model: the first, or any newer than the last.
bool ComputeClockFeeds(const SnapClockModel& m, int gameTic);

// A fit over enough samples to date ages by.
bool ComputeClockTrusted(const SnapClockModel& m);

// The game tic the trusted model puts at `ms`; false without trust.
bool ComputeClockTicAt(const SnapClockModel& m, unsigned int ms, double& tic);

// The clock model's time for a record's game tic, as its age's origin: the
// fit's line, baseMS + intercept + slope * (tic - baseTic).
double ComputeClockDate(unsigned int baseMS, double intercept, double slope,
	int baseTic, int tic);

}} // namespace zx::netsnapshot

// What dates a sample's ages: arrival until the model is trusted, and while
// its window jitter is past maxJitterMS (0 never), else the model.
enum { SNAP_AGE_UNTRUSTED, SNAP_AGE_JITTERY, SNAP_AGE_MODEL };

namespace zx { namespace netsnapshot {

int ComputeAgeSource(bool bTrusted, double windowJitterMS, int maxJitterMS);

}} // namespace zx::netsnapshot

#endif // __SNAP_CLOCK_COMPUTE_H__
