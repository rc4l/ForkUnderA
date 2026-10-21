// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/rowstep_compute.h"

using zx::RowStepIn;
using zx::RowStepOut;
using zx::StepRow;

namespace
{

const int kRefreshTimeoutMs = 4000;
const int kMaxRecheckMisses = 3;

RowStepIn Waiting( int msSinceQuery )
{
	RowStepIn in;
	in.waitingForReply = true;
	in.msSinceQuery = msSinceQuery;
	in.punchBudgetLeft = true;
	in.refreshTimeoutMs = kRefreshTimeoutMs;
	return in;
}

RowStepIn Rechecking( int msSinceRefresh )
{
	RowStepIn in;
	in.refreshing = true;
	in.msSinceRefresh = msSinceRefresh;
	in.punchBudgetLeft = true;
	in.refreshTimeoutMs = kRefreshTimeoutMs;
	in.maxRecheckFailures = kMaxRecheckMisses;
	return in;
}

bool DoesNothing( const RowStepOut &out )
{
	return !out.requestPunch && !out.resendChallenge && !out.dropFromList && !out.markTimedOut
		&& !out.recheckMissed;
}

int ActionCount( const RowStepOut &out )
{
	// recheckMissed is deliberately not counted: it accompanies dropFromList on the final strike
	// rather than competing with it, so a step can legitimately carry both.
	return ( out.requestPunch ? 1 : 0 ) + ( out.resendChallenge ? 1 : 0 )
		+ ( out.dropFromList ? 1 : 0 ) + ( out.markTimedOut ? 1 : 0 );
}

} // namespace

// ------------------------------------------------------------ nothing to do

TEST( RowStep, ARowNobodyIsWaitingOnGetsNoWork )
{
	RowStepIn in;
	in.msSinceQuery = 99999;
	in.msSinceRefresh = 99999;

	EXPECT_TRUE( DoesNothing( StepRow( in )));
}

TEST( RowStep, AQueuedRowIsNotPunishedForOurScheduling )
{
	// Not sent yet. The row is waiting on US, and starting its clock here would time it out for
	// something it did not do.
	EXPECT_TRUE( DoesNothing( StepRow( Waiting( 0 ))));
	EXPECT_TRUE( DoesNothing( StepRow( Rechecking( 0 ))));
	EXPECT_TRUE( DoesNothing( StepRow( Waiting( -5 ))));
}

// ------------------------------------------------------- the punch ladder

TEST( RowStep, BothKindsOfWaitingReachThePunchLadder )
{
	// [rc4l] THE bug, in one test. The ladder used to run only for first queries, so a server seen
	// once that then moved behind carrier NAT was re-checked, ignored and dropped without a single
	// punch request. Both kinds of waiting must be able to ask.
	bool waitingCanPunch = false;
	bool recheckCanPunch = false;

	for ( int ms = 1; ms <= 3900; ms += 25 )
	{
		if ( StepRow( Waiting( ms )).requestPunch )
			waitingCanPunch = true;
		if ( StepRow( Rechecking( ms )).requestPunch )
			recheckCanPunch = true;
	}

	EXPECT_TRUE( waitingCanPunch );
	EXPECT_TRUE( recheckCanPunch );
}

TEST( RowStep, ALanRowIsNeverPunched )
{
	// There is no NAT between us, so an introduction opens nothing.
	for ( int ms = 1; ms <= 3900; ms += 25 )
	{
		RowStepIn in = Waiting( ms );
		in.lan = true;
		EXPECT_FALSE( StepRow( in ).requestPunch ) << "ms " << ms;
	}
}

TEST( RowStep, NoBudgetMeansNoNewIntroduction )
{
	for ( int ms = 1; ms <= 3900; ms += 25 )
	{
		RowStepIn in = Waiting( ms );
		in.punchBudgetLeft = false;
		in.punchRequested = false;
		EXPECT_FALSE( StepRow( in ).requestPunch ) << "ms " << ms;
	}
}

TEST( RowStep, ARowThatAlreadyAskedKeepsItsLadderWithoutBudget )
{
	// Budget buys the ASK. A row that already spent one is allowed to finish its ladder, or the
	// resends it is owed would be cancelled by a queue it is no longer in.
	bool resent = false;
	for ( int ms = 1; ms <= 3900; ms += 25 )
	{
		RowStepIn in = Waiting( ms );
		in.punchBudgetLeft = false;
		in.punchRequested = true;
		if ( StepRow( in ).resendChallenge )
			resent = true;
	}
	EXPECT_TRUE( resent );
}

// ---------------------------------------------------------- giving up

TEST( RowStep, OneMissedRecheckDoesNotTakeTheServerAway )
{
	// The bug this exists to stop: a live server vanished off the list because a single re-check
	// datagram went missing, and only a manual refresh brought it back.
	const RowStepOut out = StepRow( Rechecking( kRefreshTimeoutMs ));

	EXPECT_TRUE( out.recheckMissed );
	EXPECT_FALSE( out.dropFromList );
	EXPECT_FALSE( out.markTimedOut );
}

TEST( RowStep, MissingEveryRecheckInARowDoesTakeItAway )
{
	// It answered once and has now ignored the whole allowance. That is gone rather than unlucky,
	// and leaving it up offers a server nobody can join.
	RowStepIn in = Rechecking( kRefreshTimeoutMs );
	in.recheckFailures = kMaxRecheckMisses - 1;

	const RowStepOut out = StepRow( in );

	EXPECT_TRUE( out.recheckMissed );
	EXPECT_TRUE( out.dropFromList );
}

TEST( RowStep, TheStrikesHaveToRunOutBeforeTheRowDoes )
{
	// Swept, because an off-by-one here is the difference between three chances and one, and the
	// symptom of getting it wrong looks exactly like the bug that is being fixed.
	for ( int failures = 0; failures < kMaxRecheckMisses; ++failures )
	{
		RowStepIn in = Rechecking( kRefreshTimeoutMs );
		in.recheckFailures = failures;

		const RowStepOut out = StepRow( in );
		const bool bLast = ( failures + 1 >= kMaxRecheckMisses );

		EXPECT_TRUE( out.recheckMissed ) << "failures " << failures;
		EXPECT_EQ( bLast, out.dropFromList ) << "failures " << failures;
	}
}

TEST( RowStep, NoAllowanceConfiguredMeansTheRowIsNeverDropped )
{
	// maxRecheckFailures of zero is a caller that has not opted in. Reading it as "drop on the
	// first miss" would restore the bug by way of a default.
	RowStepIn in = Rechecking( kRefreshTimeoutMs );
	in.maxRecheckFailures = 0;
	in.recheckFailures = 99;

	const RowStepOut out = StepRow( in );

	EXPECT_TRUE( out.recheckMissed );
	EXPECT_FALSE( out.dropFromList );
}

TEST( RowStep, AFirstQueryThatFailsSaysSoInsteadOfVanishing )
{
	// The registry lists it, so somebody thinks it exists. "We asked and got nothing" is worth
	// saying, and is a different fact from "it is gone".
	bool timedOut = false;
	for ( int ms = 1; ms <= 20000; ms += 25 )
	{
		if ( StepRow( Waiting( ms )).markTimedOut )
			timedOut = true;
	}

	EXPECT_TRUE( timedOut );
}

TEST( RowStep, ARecheckNeverMarksTimedOut )
{
	for ( int ms = 1; ms <= 20000; ms += 25 )
		EXPECT_FALSE( StepRow( Rechecking( ms )).markTimedOut ) << "ms " << ms;
}

TEST( RowStep, ARecheckWithNoDeadlineIsNeverDropped )
{
	RowStepIn in = Rechecking( 999999 );
	in.refreshTimeoutMs = 0;

	EXPECT_FALSE( StepRow( in ).dropFromList );
}

// -------------------------------------------------- the whole state space

TEST( RowStep, NeverTwoActionsAtOnce )
{
	// Every output is something the caller DOES. Two at once means the caller has to invent a
	// precedence rule, which is exactly the kind of judgement that belongs here and not there.
	for ( int bits = 0; bits < 64; ++bits )
	{
		RowStepIn in;
		in.refreshing      = (( bits & 1 ) != 0 );
		in.waitingForReply = (( bits & 2 ) != 0 );
		in.lan             = (( bits & 4 ) != 0 );
		in.punchRequested  = (( bits & 8 ) != 0 );
		in.punchBudgetLeft = (( bits & 16 ) != 0 );
		in.refreshTimeoutMs = (( bits & 32 ) != 0 ) ? kRefreshTimeoutMs : 0;

		for ( int ms = -50; ms <= 12000; ms += 137 )
		{
			in.msSinceQuery = ms;
			in.msSinceRefresh = ms;

			for ( int resends = 0; resends <= 4; ++resends )
			{
				in.resends = resends;
				EXPECT_LE( ActionCount( StepRow( in )), 1 )
					<< "bits " << bits << " ms " << ms << " resends " << resends;
			}
		}
	}
}

TEST( RowStep, ARowNobodyAwaitsIsNeverActedOn )
{
	// Swept over the whole space rather than asserted once: the caller must never be handed work for
	// a row it is not waiting on, whatever else is true of it.
	for ( int bits = 0; bits < 16; ++bits )
	{
		RowStepIn in;
		in.lan             = (( bits & 1 ) != 0 );
		in.punchRequested  = (( bits & 2 ) != 0 );
		in.punchBudgetLeft = (( bits & 4 ) != 0 );
		in.refreshTimeoutMs = (( bits & 8 ) != 0 ) ? kRefreshTimeoutMs : 0;

		for ( int ms = -50; ms <= 12000; ms += 311 )
		{
			in.msSinceQuery = ms;
			in.msSinceRefresh = ms;
			EXPECT_TRUE( DoesNothing( StepRow( in ))) << "bits " << bits << " ms " << ms;
		}
	}
}

TEST( RowStep, DroppingIsOnlyEverARecheck )
{
	// A first query must never delete a row the registry is still offering; it can only report that
	// nothing answered.
	for ( int ms = -50; ms <= 20000; ms += 97 )
		EXPECT_FALSE( StepRow( Waiting( ms )).dropFromList ) << "ms " << ms;
}
