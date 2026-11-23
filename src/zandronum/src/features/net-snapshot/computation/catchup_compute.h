// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: catchup_compute.h
//
// Description: The server's catch-up clamp: how many tics one pump may run,
// how many it drops, and when a run of dropping pumps has lasted long enough
// to be load rather than a stall. Pure: capture.cpp reads the setting, the
// clock and the level, keeps the run and prints what these return (design doc
// 5.4).
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_CATCHUP_COMPUTE_H__
#define __SNAP_CATCHUP_COMPUTE_H__

// Pumps that must drop in a row, and how long the run must last, before a
// warning: a level load stalls the loop and drops a short burst that is not
// load at all.
#define SNAPCATCHUP_MIN_PUMPS	3
#define SNAPCATCHUP_RUN_MS		1000
// The warning's rate limit and the grace after a level load, in tic rates.
#define SNAPCATCHUP_WARN_SECS	5
#define SNAPCATCHUP_LOAD_SECS	2

// One run of pumps that dropped tics, as the caller keeps it between pumps.
struct SnapCatchUpRun
{
	unsigned int	pumps;			// pumps that have dropped, in a row
	unsigned int	startMS;		// when this run began
	unsigned int	processedTics;	// tics run over the run
	unsigned int	droppedTics;	// tics dropped over it
	int				lastWarnTic;	// the gametic the last warning printed at
};

// What one pump does.
struct SnapCatchUpOut
{
	unsigned int	tics;		// tics to run now
	unsigned int	dropped;	// tics dropped now, 0 while the load is carried
	bool			bWarn;		// the run is sustained: say so
	int				percent;	// game time as a percentage of real time over the run
};

namespace zx { namespace netsnapshot {

// The clamp and the run's verdict. `maxTics` at or below 0 is uncapped; a pump that drops nothing
// ends the run, so "sustained" means pumps that dropped without a clean one between them.
SnapCatchUpOut ComputeCatchUp( unsigned int deltaTics, int maxTics, unsigned int nowMS,
	int gametic, int mapTime, unsigned int ticRate, SnapCatchUpRun & run );

}} // namespace zx::netsnapshot

#endif // __SNAP_CATCHUP_COMPUTE_H__
