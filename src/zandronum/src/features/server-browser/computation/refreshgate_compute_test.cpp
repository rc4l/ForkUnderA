// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/refreshgate_compute.h"

using zx::GateRefresh;
using zx::RefreshGateIn;
using zx::RefreshGateOut;

namespace
{

const int kFloorMs = 10000;

RefreshGateIn At( int msSinceLastRefresh )
{
	RefreshGateIn in;
	in.msSinceLastRefresh = msSinceLastRefresh;
	in.minIntervalMs = kFloorMs;
	return in;
}

} // namespace

TEST( RefreshGate, AListThatWasNeverRefreshedIsAlwaysAllowed )
{
	// The floor measures time since a sweep that has not happened. Blocking here would leave a
	// browser opened in the first seconds of a session with no route to its first server.
	RefreshGateIn in = At( -1 );

	const RefreshGateOut out = GateRefresh( in );

	EXPECT_TRUE( out.allowed );
	EXPECT_EQ( 0, out.waitSeconds );
}

TEST( RefreshGate, NoFloorConfiguredMeansNeverBlocked )
{
	// Zero is a caller that has not opted in. Reading it as "block everything" would disable the
	// button by way of a default.
	RefreshGateIn in = At( 0 );
	in.minIntervalMs = 0;

	EXPECT_TRUE( GateRefresh( in ).allowed );
}

TEST( RefreshGate, PressingAgainImmediatelyIsRefused )
{
	const RefreshGateOut out = GateRefresh( At( 0 ));

	EXPECT_FALSE( out.allowed );
	EXPECT_EQ( 10, out.waitSeconds );
}

TEST( RefreshGate, WaitingOutTheFloorAllowsIt )
{
	EXPECT_TRUE( GateRefresh( At( kFloorMs )).allowed );
	EXPECT_TRUE( GateRefresh( At( kFloorMs + 1 )).allowed );
	EXPECT_TRUE( GateRefresh( At( 999999 )).allowed );
}

TEST( RefreshGate, TheLastMillisecondStillCountsAsAWholeSecond )
{
	// Rounding DOWN here would show "0 seconds" beside a button that still refuses, which reads as
	// broken rather than as busy.
	const RefreshGateOut out = GateRefresh( At( kFloorMs - 1 ));

	EXPECT_FALSE( out.allowed );
	EXPECT_EQ( 1, out.waitSeconds );
}

TEST( RefreshGate, TheCountdownNeverLiesAboutHowLongIsLeft )
{
	// Swept, because the whole point of the number is that waiting exactly that long works. Showing
	// more than is owed makes the player wait for nothing; showing less makes the button refuse
	// after its own countdown said go.
	for ( int since = 0; since < kFloorMs; ++since )
	{
		const RefreshGateOut out = GateRefresh( At( since ));

		ASSERT_FALSE( out.allowed ) << "since " << since;
		ASSERT_GE( out.waitSeconds, 1 ) << "since " << since;

		// Waiting the advertised time is enough...
		EXPECT_TRUE( GateRefresh( At( since + out.waitSeconds * 1000 )).allowed ) << "since " << since;

		// ...and no whole second less would have been.
		if ( out.waitSeconds > 1 )
		{
			EXPECT_FALSE( GateRefresh( At( since + ( out.waitSeconds - 1 ) * 1000 )).allowed )
				<< "since " << since;
		}
	}
}

TEST( RefreshGate, ANegativeFloorIsNoFloorRatherThanAnInvertedOne )
{
	RefreshGateIn in = At( 0 );
	in.minIntervalMs = -5000;

	EXPECT_TRUE( GateRefresh( in ).allowed );
}
