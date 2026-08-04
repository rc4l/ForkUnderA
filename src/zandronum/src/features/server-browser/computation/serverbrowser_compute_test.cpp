// [rc4l] Tests for the server browser's presentation logic. Every line/branch (the coverage gate
// enforces 100% on *_compute.cpp).
//
// The cases that matter most are the ones the old browser got wrong: telling "still looking" apart
// from "nothing there", and keeping the list still while results arrive underneath the cursor.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/server-browser/computation/serverbrowser_compute.h"

#include <gtest/gtest.h>

using zx::BrowserCounts;
using zx::BrowserPhase;
using zx::ComputeBrowserPhase;
using zx::ComputeClampedSelection;
using zx::ComputePingBucket;
using zx::ComputeRowWindow;
using zx::ComputeShowsProgress;
using zx::ComputeSpinnerFrame;
using zx::PingBucket;
using zx::RowWindow;

namespace
{
BrowserCounts Counts( int waiting, int active, int timedOut, int bad )
{
	BrowserCounts c;
	c.waiting = waiting;
	c.active = active;
	c.timedOut = timedOut;
	c.badResponse = bad;
	return c;
}
}

// ---- BrowserPhase: the distinction the old browser could not make ----------

TEST( BrowserPhase, NothingKnownYetIsLoadingNotEmpty )
{
	// Registry not yet answered: we do not know whether any server exists.
	EXPECT_EQ( ComputeBrowserPhase( true, Counts( 0, 0, 0, 0 )), BrowserPhase::Loading );
	// Registry answered, servers asked, none replied yet.
	EXPECT_EQ( ComputeBrowserPhase( false, Counts( 5, 0, 0, 0 )), BrowserPhase::Loading );
}

TEST( BrowserPhase, GenuinelyEmptyOnlyWhenNothingIsOutstanding )
{
	// The registry answered with no servers at all.
	EXPECT_EQ( ComputeBrowserPhase( false, Counts( 0, 0, 0, 0 )), BrowserPhase::Empty );

	// Every server we knew about failed. Still Empty -- those failures are the REASON the list is
	// empty, not a reason to keep spinning forever.
	EXPECT_EQ( ComputeBrowserPhase( false, Counts( 0, 0, 3, 0 )), BrowserPhase::Empty );
	EXPECT_EQ( ComputeBrowserPhase( false, Counts( 0, 0, 0, 2 )), BrowserPhase::Empty );
	EXPECT_EQ( ComputeBrowserPhase( false, Counts( 0, 0, 1, 1 )), BrowserPhase::Empty );
}

TEST( BrowserPhase, AnythingDrawableBeatsWaiting )
{
	// One server answered while nine are outstanding: show the one, do not hide it behind a spinner.
	EXPECT_EQ( ComputeBrowserPhase( false, Counts( 9, 1, 0, 0 )), BrowserPhase::Ready );
	// Even mid-registry-refresh, existing results stay on screen.
	EXPECT_EQ( ComputeBrowserPhase( true, Counts( 0, 4, 0, 0 )), BrowserPhase::Ready );
	EXPECT_EQ( ComputeBrowserPhase( false, Counts( 0, 2, 5, 3 )), BrowserPhase::Ready );
}

TEST( ShowsProgress, TrueWheneverSomethingIsStillOutstanding )
{
	// Ready and "still working" are simultaneously true -- which is why this is a separate question.
	EXPECT_TRUE( ComputeShowsProgress( false, Counts( 3, 5, 0, 0 )));
	EXPECT_TRUE( ComputeShowsProgress( true, Counts( 0, 5, 0, 0 )));
	EXPECT_TRUE( ComputeShowsProgress( true, Counts( 0, 0, 0, 0 )));

	// Nothing outstanding: failures are settled, not pending.
	EXPECT_FALSE( ComputeShowsProgress( false, Counts( 0, 5, 0, 0 )));
	EXPECT_FALSE( ComputeShowsProgress( false, Counts( 0, 0, 4, 1 )));
}

// ---- Spinner ---------------------------------------------------------------

TEST( SpinnerFrame, AdvancesAndWraps )
{
	EXPECT_EQ( ComputeSpinnerFrame( 0, 4, 3 ), 0 );
	EXPECT_EQ( ComputeSpinnerFrame( 2, 4, 3 ), 0 );  // same frame until ticsPerFrame elapses
	EXPECT_EQ( ComputeSpinnerFrame( 3, 4, 3 ), 1 );
	EXPECT_EQ( ComputeSpinnerFrame( 11, 4, 3 ), 3 );
	EXPECT_EQ( ComputeSpinnerFrame( 12, 4, 3 ), 0 ); // wraps
}

TEST( SpinnerFrame, RefusesToCrashOnNonsense )
{
	EXPECT_EQ( ComputeSpinnerFrame( 10, 0, 3 ), 0 );   // no frames: would divide by zero
	EXPECT_EQ( ComputeSpinnerFrame( 10, -1, 3 ), 0 );
	EXPECT_EQ( ComputeSpinnerFrame( 10, 4, 0 ), 0 );
	EXPECT_EQ( ComputeSpinnerFrame( 10, 4, -2 ), 0 );
	EXPECT_EQ( ComputeSpinnerFrame( -5, 4, 3 ), 0 );   // negative tic must not index backwards
}

// ---- Ping buckets ----------------------------------------------------------

TEST( PingBucket, BoundariesAreExact )
{
	EXPECT_EQ( ComputePingBucket( -1 ), PingBucket::Unknown );
	EXPECT_EQ( ComputePingBucket( 0 ), PingBucket::Good );
	EXPECT_EQ( ComputePingBucket( 79 ), PingBucket::Good );
	EXPECT_EQ( ComputePingBucket( 80 ), PingBucket::Fair );
	EXPECT_EQ( ComputePingBucket( 159 ), PingBucket::Fair );
	EXPECT_EQ( ComputePingBucket( 160 ), PingBucket::Poor );
	EXPECT_EQ( ComputePingBucket( 9999 ), PingBucket::Poor );
}

// ---- Row window ------------------------------------------------------------

TEST( RowWindow, ShortListShowsEverythingFromTheTop )
{
	const RowWindow w = ComputeRowWindow( 3, 10, 0, 0 );
	EXPECT_EQ( w.first, 0 );
	EXPECT_EQ( w.count, 3 ); // count is the rows that EXIST, not the page size
}

TEST( RowWindow, EmptyOrDegenerateInputDrawsNothing )
{
	EXPECT_EQ( ComputeRowWindow( 0, 10, -1, 0 ).count, 0 );
	EXPECT_EQ( ComputeRowWindow( -5, 10, 0, 0 ).count, 0 );
	EXPECT_EQ( ComputeRowWindow( 10, 0, 0, 0 ).count, 0 );
	EXPECT_EQ( ComputeRowWindow( 10, -1, 0, 0 ).count, 0 );
}

TEST( RowWindow, ScrollsOnlyAsFarAsNeededToRevealTheSelection )
{
	// Selection below the window: scroll down by exactly one row, not a whole page and not a recentre.
	RowWindow w = ComputeRowWindow( 50, 8, 8, 0 );
	EXPECT_EQ( w.first, 1 );
	EXPECT_EQ( w.count, 8 );

	// Selection above the window: scroll up to sit on it.
	w = ComputeRowWindow( 50, 8, 4, 10 );
	EXPECT_EQ( w.first, 4 );

	// Selection already visible: the window does not move at all. This is what keeps the list still
	// while servers stream in.
	w = ComputeRowWindow( 50, 8, 12, 10 );
	EXPECT_EQ( w.first, 10 );
}

TEST( RowWindow, RecoversWhenTheListShrinksUnderneath )
{
	// Scrolled to row 40 of 50, then the list collapses to 12 entries.
	const RowWindow w = ComputeRowWindow( 12, 8, -1, 40 );
	EXPECT_EQ( w.first, 4 );  // pinned to the last full page
	EXPECT_EQ( w.count, 8 );
}

TEST( RowWindow, IgnoresAnOutOfRangeOrAbsentSelection )
{
	// No selection: the window is left where it was.
	EXPECT_EQ( ComputeRowWindow( 50, 8, -1, 6 ).first, 6 );
	// Selection past the end (list shrank but the caller has not clamped yet): must not scroll to it.
	EXPECT_EQ( ComputeRowWindow( 50, 8, 999, 6 ).first, 6 );
	// Negative currentFirst is clamped rather than producing a negative index.
	EXPECT_EQ( ComputeRowWindow( 50, 8, -1, -3 ).first, 0 );
}

TEST( RowWindow, LastPageIsPartialNotOverrun )
{
	const RowWindow w = ComputeRowWindow( 10, 4, 9, 7 );
	EXPECT_EQ( w.first, 6 );
	EXPECT_EQ( w.count, 4 );
	EXPECT_LE( w.first + w.count, 10 ); // never reads past the end
}

// ---- Selection clamping ----------------------------------------------------

TEST( ClampedSelection, TracksAListChangingUnderIt )
{
	EXPECT_EQ( ComputeClampedSelection( 3, 10 ), 3 );   // unchanged when valid
	EXPECT_EQ( ComputeClampedSelection( 20, 10 ), 9 );  // list shrank -> last row
	EXPECT_EQ( ComputeClampedSelection( -1, 10 ), 0 );  // first result arrives -> select it
	EXPECT_EQ( ComputeClampedSelection( 5, 0 ), -1 );   // list emptied -> nothing selected
	EXPECT_EQ( ComputeClampedSelection( -1, 0 ), -1 );
	EXPECT_EQ( ComputeClampedSelection( 0, -2 ), -1 );  // nonsense total
}
