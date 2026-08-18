// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/federated-server-registry/computation/cookieclaim_compute.h"

using namespace zx;

TEST(CookieClaim, AProbeConsumesItsCookie)
{
	// One cookie buys one probe, because a probe makes the registry send a packet at an address and
	// a replayable one turns it into a packet cannon.
	const CookieClaim claim = DecideCookieClaim( true, CookiePurpose::ReachProbe );
	EXPECT_TRUE( claim.accepted );
	EXPECT_TRUE( claim.consume );
}

TEST(CookieClaim, APunchDoesNotConsumeItsCookie)
{
	// THE BUG THIS UNIT EXISTS FOR. A launcher refreshing its list asks about several servers at
	// once and is handed the same cookie for each, so consuming it made the first punch of a sweep
	// work and refused every one after it.
	const CookieClaim claim = DecideCookieClaim( true, CookiePurpose::Punch );
	EXPECT_TRUE( claim.accepted );
	EXPECT_FALSE( claim.consume );
}

TEST(CookieClaim, AnUnmatchedCookieProvesNothing)
{
	EXPECT_FALSE( DecideCookieClaim( false, CookiePurpose::Punch ).accepted );
	EXPECT_FALSE( DecideCookieClaim( false, CookiePurpose::ReachProbe ).accepted );
}

TEST(CookieClaim, AFailedClaimNeverDestroysAnything)
{
	// Otherwise guessing at somebody else's cookie would be a way to cancel it.
	EXPECT_FALSE( DecideCookieClaim( false, CookiePurpose::ReachProbe ).consume );
	EXPECT_FALSE( DecideCookieClaim( false, CookiePurpose::Punch ).consume );
}

TEST(CookieClaim, ConsumingIsDecidedByPurposeAloneAndNothingElse)
{
	// The property that keeps the two callers from drifting: purpose decides, and an accepted claim
	// is consumed if and only if it was a probe.
	for ( int f = 0; f <= 1; ++f )
	{
		for ( int p = 0; p <= 1; ++p )
		{
			const bool found = ( f == 1 );
			const CookiePurpose purpose = ( p == 1 ) ? CookiePurpose::ReachProbe : CookiePurpose::Punch;
			const CookieClaim claim = DecideCookieClaim( found, purpose );

			EXPECT_EQ( found, claim.accepted );
			EXPECT_EQ( found && ( purpose == CookiePurpose::ReachProbe ), claim.consume );
		}
	}
}

TEST(CookieClaim, ADefaultClaimProvesNothingAndDestroysNothing)
{
	// The safe reading of "nobody has decided yet", so a caller that forgets to assign one cannot
	// accidentally admit somebody or delete their cookie.
	const CookieClaim claim;
	EXPECT_FALSE( claim.accepted );
	EXPECT_FALSE( claim.consume );
}
