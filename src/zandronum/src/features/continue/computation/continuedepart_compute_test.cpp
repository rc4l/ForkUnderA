// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/continue/computation/continuedepart_compute.h"

using namespace zx;

namespace
{

ContinueDepartInputs LeftAServer()
{
	ContinueDepartInputs in;
	in.wasInSession = true;
	return in;
}

} // namespace

TEST( ContinueDepart, LeavingAServerTakesYouBack )
{
	EXPECT_EQ( ContinueDepartVerdict::Return, DecideContinueDepart( LeftAServer() ));
}

TEST( ContinueDepart, EveryWayOfLeavingIsTheSameWayOfLeaving )
{
	// Disconnect, kicked, banned, server died, version mismatch: the caller does not distinguish
	// them and neither does this. A player who lands somewhere different depending on WHY they left
	// has to understand the difference to predict the game.
	EXPECT_EQ( ContinueDepartVerdict::Return, DecideContinueDepart( LeftAServer() ));
}

TEST( ContinueDepart, AJoinInFlightIsNotADeparture )
{
	// CLIENT_QuitNetworkGame is the teardown for EVERYTHING, including the tidy-up a successful
	// join does on its way IN. Acting on that would drag the player out of the join they are half
	// way through and into an old single-player game.
	ContinueDepartInputs in = LeftAServer();
	in.joinInFlight = true;

	EXPECT_EQ( ContinueDepartVerdict::Ignore, DecideContinueDepart( in ));
}

TEST( ContinueDepart, ReconnectIsNotADeparture )
{
	// It tears down the connection on purpose, on its way back to the same server. Returning the
	// player to an offline game first would make reconnect impossible to use.
	ContinueDepartInputs in = LeftAServer();
	in.reconnecting = true;

	EXPECT_EQ( ContinueDepartVerdict::Ignore, DecideContinueDepart( in ));
}

TEST( ContinueDepart, ACrashIsNotADeparture )
{
	ContinueDepartInputs in = LeftAServer();
	in.crashing = true;

	EXPECT_EQ( ContinueDepartVerdict::Ignore, DecideContinueDepart( in ));
}

TEST( ContinueDepart, TearingDownSomethingThatWasNotASessionDoesNothing )
{
	ContinueDepartInputs in;
	in.wasInSession = false;

	EXPECT_EQ( ContinueDepartVerdict::Ignore, DecideContinueDepart( in ));
}

TEST( ContinueDepart, AnythingUnsureIsIgnored )
{
	// The asymmetry that governs the whole unit: a missed return leaves the player where they
	// already are, a wrong one destroys a connection they were making.
	for ( int join = 0; join <= 1; ++join )
	{
		for ( int recon = 0; recon <= 1; ++recon )
		{
			for ( int crash = 0; crash <= 1; ++crash )
			{
				ContinueDepartInputs in = LeftAServer();
				in.joinInFlight = ( join == 1 );
				in.reconnecting = ( recon == 1 );
				in.crashing = ( crash == 1 );

				const bool expected = ( join == 0 ) && ( recon == 0 ) && ( crash == 0 );
				EXPECT_EQ( expected, DecideContinueDepart( in ) == ContinueDepartVerdict::Return )
					<< "join=" << join << " recon=" << recon << " crash=" << crash;
			}
		}
	}
}
