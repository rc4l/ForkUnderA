//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: catchup_compute.cpp
//
// Description: catchup_compute.h's clamp and run decision.
//
//-----------------------------------------------------------------------------

#include "features/net-snapshot/computation/catchup_compute.h"

SnapCatchUpOut zx::netsnapshot::ComputeCatchUp( unsigned int deltaTics, int maxTics,
	unsigned int nowMS, int gametic, int mapTime, unsigned int ticRate, SnapCatchUpRun & run )
{
	SnapCatchUpOut out;
	out.tics = deltaTics;
	out.dropped = 0;
	out.bWarn = false;
	out.percent = 100;

	// Uncapped, or inside the cap: the run ends here, so a burst that stops is not carried into
	// the next one.
	if (( maxTics <= 0 ) || ( deltaTics <= static_cast<unsigned int>( maxTics )))
	{
		run.pumps = 0;
		return out;
	}

	out.tics = static_cast<unsigned int>( maxTics );
	out.dropped = deltaTics - out.tics;

	if ( run.pumps == 0 )
	{
		run.startMS = nowMS;
		run.processedTics = 0;
		run.droppedTics = 0;
	}
	++run.pumps;
	run.processedTics += out.tics;
	run.droppedTics += out.dropped;

	// The dilation over the whole run, which is what an operator is being told; never over no
	// tics, since a dropping pump runs at least the cap.
	const unsigned int total = run.processedTics + run.droppedTics;
	out.percent = static_cast<int>(( 100.0 * run.processedTics / total ) + 0.5 );

	// Measured in time, not in pumps: a stall drops a short burst at any load.
	const bool bSustained = ( run.pumps >= SNAPCATCHUP_MIN_PUMPS )
		&& (( nowMS - run.startMS ) >= SNAPCATCHUP_RUN_MS );
	// A level load is a stall we can name, so its first seconds are skipped.
	const bool bJustLoaded = ( mapTime < static_cast<int>( ticRate * SNAPCATCHUP_LOAD_SECS ));
	if ( bSustained && ( bJustLoaded == false )
		&& (( gametic - run.lastWarnTic ) >= static_cast<int>( ticRate * SNAPCATCHUP_WARN_SECS )))
	{
		out.bWarn = true;
		run.lastWarnTic = gametic;
	}
	return out;
}
