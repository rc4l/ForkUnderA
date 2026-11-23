// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: catchup_compute_test.cpp
//
// Description: Tests for catchup_compute: the clamp at and past the cap and
// uncapped, the run's start, break and dilation, and the warning's three
// conditions -- the run's length, a level load's grace and the rate limit.
//
//-----------------------------------------------------------------------------

#include "catchup_compute.h"
#include <gtest/gtest.h>

using namespace zx::netsnapshot;

namespace
{

const unsigned int RATE = 35;
// Past the load grace, so a test that is not about it is not caught by it.
const int SETTLED = (int)( RATE * SNAPCATCHUP_LOAD_SECS ) + 1;

SnapCatchUpRun FreshRun()
{
	SnapCatchUpRun run;
	run.pumps = 0;
	run.startMS = 0;
	run.processedTics = 0;
	run.droppedTics = 0;
	run.lastWarnTic = -( (int)( RATE * SNAPCATCHUP_WARN_SECS ));
	return run;
}

// A pump that drops, with the clock and the gametic moving as they would.
SnapCatchUpOut Pump( SnapCatchUpRun & run, unsigned int deltaTics, unsigned int nowMS, int tic )
{
	return ComputeCatchUp( deltaTics, 8, nowMS, tic, SETTLED, RATE, run );
}

} // namespace

// ---- the clamp ----

TEST(SnapCatchUp, InsideTheCapRunsEveryTic)
{
	SnapCatchUpRun run = FreshRun();
	const SnapCatchUpOut out = Pump( run, 8, 1000, 100 );
	EXPECT_EQ( 8u, out.tics );
	EXPECT_EQ( 0u, out.dropped );
	EXPECT_FALSE( out.bWarn );
	EXPECT_EQ( 100, out.percent );
	EXPECT_EQ( 0u, run.pumps );
}

TEST(SnapCatchUp, PastTheCapDropsTheRest)
{
	SnapCatchUpRun run = FreshRun();
	const SnapCatchUpOut out = Pump( run, 20, 1000, 100 );
	EXPECT_EQ( 8u, out.tics );
	EXPECT_EQ( 12u, out.dropped );
	EXPECT_EQ( 1u, run.pumps );
	EXPECT_EQ( 1000u, run.startMS );
	// 8 of 20 tics ran: 40% of real time.
	EXPECT_EQ( 40, out.percent );
}

TEST(SnapCatchUp, UncappedRunsEverythingAndEndsTheRun)
{
	SnapCatchUpRun run = FreshRun();
	run.pumps = 4;
	for ( int maxTics = 0; maxTics >= -1; --maxTics )
	{
		const SnapCatchUpOut out = ComputeCatchUp( 90, maxTics, 2000, 200, SETTLED, RATE, run );
		EXPECT_EQ( 90u, out.tics ) << maxTics;
		EXPECT_EQ( 0u, out.dropped ) << maxTics;
		EXPECT_EQ( 0u, run.pumps ) << maxTics;
	}
}

// ---- the run ----

TEST(SnapCatchUp, ACleanPumpEndsTheRun)
{
	SnapCatchUpRun run = FreshRun();
	Pump( run, 20, 1000, 100 );
	Pump( run, 20, 1100, 104 );
	EXPECT_EQ( 2u, run.pumps );
	// One pump inside the cap, and the next dropping pump starts a run of its own.
	Pump( run, 4, 1200, 108 );
	EXPECT_EQ( 0u, run.pumps );
	const SnapCatchUpOut out = Pump( run, 20, 1300, 112 );
	EXPECT_EQ( 1u, run.pumps );
	EXPECT_EQ( 1300u, run.startMS );
	EXPECT_FALSE( out.bWarn );
}

TEST(SnapCatchUp, TheDilationIsOverTheWholeRun)
{
	SnapCatchUpRun run = FreshRun();
	Pump( run, 16, 1000, 100 );			// 8 run, 8 dropped
	const SnapCatchUpOut out = Pump( run, 24, 1030, 104 );	// 8 run, 16 dropped
	EXPECT_EQ( 16u, run.processedTics );
	EXPECT_EQ( 24u, run.droppedTics );
	// 16 of 40 tics: 40%.
	EXPECT_EQ( 40, out.percent );
}

// ---- the warning ----

TEST(SnapCatchUp, WarnsOnlyOnceTheRunIsOldEnough)
{
	SnapCatchUpRun run = FreshRun();
	// Three pumps, but inside the same second: not yet.
	EXPECT_FALSE( Pump( run, 20, 1000, 100 ).bWarn );
	EXPECT_FALSE( Pump( run, 20, 1400, 104 ).bWarn );
	EXPECT_FALSE( Pump( run, 20, 1900, 108 ).bWarn );
	// The fourth is a second past the run's start, and the pump count is met.
	EXPECT_TRUE( Pump( run, 20, 2000, 112 ).bWarn );
}

TEST(SnapCatchUp, TwoPumpsNeverWarnHoweverLongTheRun)
{
	SnapCatchUpRun run = FreshRun();
	EXPECT_FALSE( Pump( run, 20, 1000, 100 ).bWarn );
	EXPECT_FALSE( Pump( run, 20, 9000, 400 ).bWarn );
	EXPECT_EQ( 2u, run.pumps );
}

TEST(SnapCatchUp, ALevelLoadIsNotLoad)
{
	SnapCatchUpRun run = FreshRun();
	const int loading = (int)( RATE * SNAPCATCHUP_LOAD_SECS ) - 1;
	for ( int i = 0; i < 4; ++i )
		EXPECT_FALSE( ComputeCatchUp( 20, 8, 1000u + 400u * i, 100 + 4 * i, loading, RATE,
			run ).bWarn ) << i;
	EXPECT_EQ( 4u, run.pumps );
}

TEST(SnapCatchUp, TheWarningIsRateLimited)
{
	SnapCatchUpRun run = FreshRun();
	Pump( run, 20, 1000, 100 );
	Pump( run, 20, 1400, 104 );
	Pump( run, 20, 1900, 108 );
	EXPECT_TRUE( Pump( run, 20, 2000, 112 ).bWarn );
	EXPECT_EQ( 112, run.lastWarnTic );
	// Inside the limit, however long the run goes on.
	EXPECT_FALSE( Pump( run, 20, 3000, 150 ).bWarn );
	// And again once the limit has passed.
	const int next = 112 + (int)( RATE * SNAPCATCHUP_WARN_SECS );
	EXPECT_TRUE( Pump( run, 20, 6000, next ).bWarn );
	EXPECT_EQ( next, run.lastWarnTic );
}

TEST(SnapCatchUp, TheClockWrapsWithoutWarningEarly)
{
	SnapCatchUpRun run = FreshRun();
	// A run that began just before the millisecond counter wrapped: the span still reads right.
	const unsigned int justBefore = 0xFFFFFF00u;
	Pump( run, 20, justBefore, 100 );
	Pump( run, 20, justBefore + 100u, 104 );
	Pump( run, 20, justBefore + 200u, 108 );
	EXPECT_FALSE( Pump( run, 20, justBefore + 500u, 112 ).bWarn );
	EXPECT_TRUE( Pump( run, 20, justBefore + 1100u, 116 ).bWarn );
}
