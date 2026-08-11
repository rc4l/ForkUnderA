// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/federated-server-registry/computation/listingproof_compute.h"

#include <string>

using zx::DecideListingProof;
using zx::DescribeListing;
using zx::ListingNeedsAttention;
using zx::ListingProof;
using zx::ListingState;

namespace
{
const int kStaleAfterMs = 120000;   // two minutes, a plausible caller value
}

// ------------------------------------------------------------- the ladder

TEST( ListingProof, NothingSentMeansNothingToBeListedAs )
{
	const ListingProof p = DecideListingProof( false, false, false, false, -1, kStaleAfterMs );

	EXPECT_EQ( ListingState::NeverAnnounced, p.state );
	EXPECT_EQ( -1, p.secondsSinceVerified );
}

TEST( ListingProof, SilenceRightAfterAnnouncingIsNotAFailure )
{
	// [rc4l] The first second or two of every host looks exactly like this. Reporting it as a
	// problem would make a working setup look broken at the moment the player is watching hardest.
	const ListingProof p = DecideListingProof( true, false, false, false, -1, kStaleAfterMs );

	EXPECT_EQ( ListingState::AwaitingAnswer, p.state );
	EXPECT_FALSE( ListingNeedsAttention( p.state ));
}

TEST( ListingProof, AnExplicitNoIsARefusal )
{
	const ListingProof p = DecideListingProof( true, true, false, false, -1, kStaleAfterMs );

	EXPECT_EQ( ListingState::Refused, p.state );
	EXPECT_TRUE( ListingNeedsAttention( p.state ));
}

TEST( ListingProof, ListedIsNotTheSameAsReachable )
{
	// The whole point of the unit. Being in the list is a claim WE made; being verified is a claim
	// the registry made, and only the second one means anybody can join.
	const ListingProof p = DecideListingProof( true, true, true, false, -1, kStaleAfterMs );

	EXPECT_EQ( ListingState::ListedUnverified, p.state );
	EXPECT_TRUE( ListingNeedsAttention( p.state ));
}

TEST( ListingProof, ReachedRecentlyIsTheOnlyGoodAnswer )
{
	const ListingProof p = DecideListingProof( true, true, true, true, 30000, kStaleAfterMs );

	EXPECT_EQ( ListingState::ListedVerified, p.state );
	EXPECT_FALSE( ListingNeedsAttention( p.state ));
	EXPECT_EQ( 30, p.secondsSinceVerified );
}

// ------------------------------------------------------------- expiry

TEST( ListingProof, AnOldCheckStopsCounting )
{
	// [rc4l] "It worked" and "it works" are different claims. Routers reboot, leases move, ISPs
	// change things; a light that never goes stale is how you confidently send someone to a server
	// nobody can reach.
	const ListingProof p = DecideListingProof( true, true, true, true, kStaleAfterMs, kStaleAfterMs );

	EXPECT_EQ( ListingState::ListedStale, p.state );
	EXPECT_TRUE( ListingNeedsAttention( p.state ));
	EXPECT_EQ( 120, p.secondsSinceVerified );
}

TEST( ListingProof, JustInsideTheWindowStillCounts )
{
	const ListingProof p = DecideListingProof( true, true, true, true, kStaleAfterMs - 1, kStaleAfterMs );

	EXPECT_EQ( ListingState::ListedVerified, p.state );
}

TEST( ListingProof, ANonPositiveWindowNeverExpires )
{
	// For a host on a fixed address, where re-checking proves nothing new.
	const ListingProof p = DecideListingProof( true, true, true, true, 999999999, 0 );

	EXPECT_EQ( ListingState::ListedVerified, p.state );
}

TEST( ListingProof, ABackwardsClockIsNotEvidence )
{
	// A negative age means the clock moved, not that the check just happened. Treated as unverified
	// so a broken clock can never read as success.
	const ListingProof p = DecideListingProof( true, true, true, true, -5, kStaleAfterMs );

	EXPECT_EQ( ListingState::ListedUnverified, p.state );
	EXPECT_EQ( -1, p.secondsSinceVerified );
}

// ------------------------------------------------------------- wording

TEST( ListingProof, EveryStateSaysSomethingDefinite )
{
	const ListingState all[] = {
		ListingState::NeverAnnounced,
		ListingState::AwaitingAnswer,
		ListingState::Refused,
		ListingState::ListedUnverified,
		ListingState::ListedVerified,
		ListingState::ListedStale,
	};

	for ( unsigned int i = 0; i < ( sizeof all / sizeof all[0] ); ++i )
	{
		const std::string text = DescribeListing( all[i] );

		EXPECT_FALSE( text.empty( )) << "state " << i;
		// No hedging: if the engine cannot tell, it must say which thing it cannot tell, not guess.
		EXPECT_EQ( std::string::npos, text.find( "probably" )) << "state " << i;
		EXPECT_EQ( std::string::npos, text.find( "should be" )) << "state " << i;
	}
}

TEST( ListingProof, TheDangerousStateNamesItsConsequence )
{
	// "Unverified" alone reads like a formality, so the sentence has to say what it costs.
	const std::string text = DescribeListing( ListingState::ListedUnverified );

	EXPECT_NE( std::string::npos, text.find( "may not be able to join" ));
}

TEST( ListingProof, OnlyProgressIsAllowedToLookLikeProgress )
{
	// Every state, both ways round, because this is the function that decides whether the player is
	// shown a problem. Waiting is not a problem; being listed without ever having been reached is,
	// however much the word "listed" sounds like success.
	EXPECT_TRUE( ListingNeedsAttention( ListingState::NeverAnnounced ));
	EXPECT_TRUE( ListingNeedsAttention( ListingState::Refused ));
	EXPECT_TRUE( ListingNeedsAttention( ListingState::ListedUnverified ));
	EXPECT_TRUE( ListingNeedsAttention( ListingState::ListedStale ));

	EXPECT_FALSE( ListingNeedsAttention( ListingState::AwaitingAnswer ));
	EXPECT_FALSE( ListingNeedsAttention( ListingState::ListedVerified ));
}

TEST( ListingProof, AProofNobodyFilledInClaimsNothing )
{
	// The default is what a caller holds before it has asked. It must not read as "announced" or as
	// "checked a moment ago", which is why the age starts at never rather than at zero.
	const ListingProof p;

	EXPECT_EQ( ListingState::NeverAnnounced, p.state );
	EXPECT_EQ( -1, p.secondsSinceVerified );
}

TEST( ListingProof, AnUnknownStateIsNotSilentlyFine )
{
	const ListingState bogus = static_cast<ListingState>( 99 );

	EXPECT_STREQ( "Unknown listing state.", DescribeListing( bogus ));
	EXPECT_TRUE( ListingNeedsAttention( bogus ));
}
