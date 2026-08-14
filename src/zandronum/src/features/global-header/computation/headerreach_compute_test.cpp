// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/global-header/computation/headerreach_compute.h"

#include <string>

using zx::ComputeHeaderReach;
using zx::HeaderReach;
using zx::HeaderReachTint;
using zx::HeaderReachTooltip;
using zx::PlayOnlineSelectable;
using zx::ReachIn;
using zx::ReachTint;

namespace
{

ReachIn Signals( bool answered, bool pending, bool local )
{
	ReachIn in;
	in.anyRegistryAnswered = answered;
	in.anyRegistryPending = pending;
	in.haveLocalNetwork = local;
	return in;
}

} // namespace

// ---------------------------------------------------------------- verdicts

TEST( HeaderReach, ARegistryAnsweringIsProofOfInternet )
{
	EXPECT_EQ( HeaderReach::Internet, ComputeHeaderReach( Signals( true, false, true )));
}

TEST( HeaderReach, NothingAnsweredButAnAddressIsLanOnly )
{
	EXPECT_EQ( HeaderReach::LanOnly, ComputeHeaderReach( Signals( false, false, true )));
}

TEST( HeaderReach, NoAnswerAndNoAddressIsOffline )
{
	EXPECT_EQ( HeaderReach::Offline, ComputeHeaderReach( Signals( false, false, false )));
}

// -------------------------------------------------------- checking is not grey

TEST( HeaderReach, StillAskingIsNeverAVerdict )
{
	// [rc4l] THE bug this unit exists to prevent. A query takes seconds, and during those seconds
	// "no answer yet" looks exactly like "no internet" to anything that only checks for an answer.
	// Painting the most prominent tab on the menu grey tells the player their network is broken
	// every time they open it, and they believe it, because it is the first thing they see.
	EXPECT_EQ( HeaderReach::Checking, ComputeHeaderReach( Signals( false, true, false )));
	EXPECT_EQ( HeaderReach::Checking, ComputeHeaderReach( Signals( false, true, true )));

	EXPECT_EQ( ReachTint::Neutral, HeaderReachTint( HeaderReach::Checking ));
	EXPECT_NE( ReachTint::Grey, HeaderReachTint( HeaderReach::Checking ));
}

TEST( HeaderReach, ProofOutranksAnOutstandingQuery )
{
	// One registry answering settles it. Waiting for the slowest of four to time out before going
	// green would leave the tab neutral for the whole window in which it is already known good.
	EXPECT_EQ( HeaderReach::Internet, ComputeHeaderReach( Signals( true, true, true )));
	EXPECT_EQ( HeaderReach::Internet, ComputeHeaderReach( Signals( true, true, false )));
}

TEST( HeaderReach, EverySignalCombinationHasAnAnswer )
{
	// Eight inputs, and none of them may fall through to a default nobody chose.
	for ( int bits = 0; bits < 8; ++bits )
	{
		const ReachIn in = Signals(( bits & 1 ) != 0, ( bits & 2 ) != 0, ( bits & 4 ) != 0 );
		const HeaderReach reach = ComputeHeaderReach( in );

		// An answered registry always means internet, whatever else is set.
		if ( in.anyRegistryAnswered )
			EXPECT_EQ( HeaderReach::Internet, reach ) << "bits " << bits;

		// Grey is reserved for a genuine absence of both, never for a query in flight.
		if ( in.anyRegistryPending )
			EXPECT_NE( ReachTint::Grey, HeaderReachTint( reach )) << "bits " << bits;
	}
}

// ------------------------------------------------------------------ tints

TEST( HeaderReach, TheThreeVerdictsGetTheThreeColours )
{
	EXPECT_EQ( ReachTint::Green, HeaderReachTint( HeaderReach::Internet ));
	EXPECT_EQ( ReachTint::Orange, HeaderReachTint( HeaderReach::LanOnly ));
	EXPECT_EQ( ReachTint::Grey, HeaderReachTint( HeaderReach::Offline ));
}

// --------------------------------------------------------------- tooltips

TEST( HeaderReach, EveryVerdictSaysSomething )
{
	const HeaderReach all[] = { HeaderReach::Checking, HeaderReach::Internet,
		HeaderReach::LanOnly, HeaderReach::Offline };

	for ( int i = 0; i < 4; ++i )
	{
		const std::string tip = HeaderReachTooltip( all[i] );
		EXPECT_FALSE( tip.empty( )) << "verdict " << i;
		EXPECT_LT( tip.size( ), static_cast<size_t>( 64 )) << "verdict " << i;
	}
}

TEST( HeaderReach, TheTooltipsAreAllDifferent )
{
	// A tooltip that reads the same in two states is one the player learns to ignore, and these are
	// the states where they most need to be told which one they are in.
	const std::string checking = HeaderReachTooltip( HeaderReach::Checking );
	const std::string internet = HeaderReachTooltip( HeaderReach::Internet );
	const std::string lan = HeaderReachTooltip( HeaderReach::LanOnly );
	const std::string offline = HeaderReachTooltip( HeaderReach::Offline );

	EXPECT_NE( checking, offline );
	EXPECT_NE( checking, internet );
	EXPECT_NE( lan, offline );
	EXPECT_NE( lan, internet );
}

// ------------------------------------------------------------ selectable

TEST( HeaderReach, OnlyAGenuineAbsenceLocksTheTab )
{
	EXPECT_FALSE( PlayOnlineSelectable( HeaderReach::Offline ));

	EXPECT_TRUE( PlayOnlineSelectable( HeaderReach::Internet ));
	EXPECT_TRUE( PlayOnlineSelectable( HeaderReach::LanOnly ));
}

TEST( HeaderReach, WaitingDoesNotLockThePlayerOut )
{
	// Making somebody wait on a timer they cannot see is worse than letting them open a list that
	// fills in a second. The tab stays pressable while the answer is still coming.
	EXPECT_TRUE( PlayOnlineSelectable( HeaderReach::Checking ));
}
