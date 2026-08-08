// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/server-hosting/computation/punchbroker_compute.h"

using namespace zx;

namespace
{

const int kMax = 5;

// Everything in order: a listed server that can punch, and a requester that proved its address.
PunchAsk Good()
{
	return PunchAsk( true, true, true, 0 );
}

} // namespace

// ---------------------------------------------------------------- brokering

TEST(PunchBroker, BrokersWhenEverythingChecksOut)
{
	EXPECT_EQ(PunchVerdict::Broker, DecidePunch(Good(), kMax));
}

TEST(PunchBroker, RefusesAServerThatCannotPunch)
{
	// The answer almost every request gets, and the reason the existing ecosystem keeps working.
	PunchAsk ask = Good();
	ask.serverSupportsPunch = false;
	EXPECT_EQ(PunchVerdict::NoSupport, DecidePunch(ask, kMax));
}

TEST(PunchBroker, RefusesAServerWeHaveNotVerified)
{
	// The shape of the attack: a stranger naming an address and asking us to dial it.
	PunchAsk ask = Good();
	ask.serverListed = false;
	EXPECT_EQ(PunchVerdict::NotListed, DecidePunch(ask, kMax));
}

TEST(PunchBroker, RefusesARequesterThatNeverEchoedTheCookie)
{
	// Without this the observed source could be forged and the host aimed at somebody who never
	// asked for a packet.
	PunchAsk ask = Good();
	ask.cookieProven = false;
	EXPECT_EQ(PunchVerdict::BadCookie, DecidePunch(ask, kMax));
}

TEST(PunchBroker, RefusesOnceTheRateIsExceeded)
{
	PunchAsk ask = Good();
	ask.recentRequests = kMax;
	EXPECT_EQ(PunchVerdict::RateLimited, DecidePunch(ask, kMax));
}

TEST(PunchBroker, TheLastRequestInsideTheWindowStillPasses)
{
	PunchAsk ask = Good();
	ask.recentRequests = kMax - 1;
	EXPECT_EQ(PunchVerdict::Broker, DecidePunch(ask, kMax));
}

TEST(PunchBroker, AZeroLimitMeansNoLimit)
{
	// So the cap can be turned off without the check inverting into "refuse everything".
	PunchAsk ask = Good();
	ask.recentRequests = 100000;
	EXPECT_EQ(PunchVerdict::Broker, DecidePunch(ask, 0));
}

// ---------------------------------------------------------------- the order of refusals

TEST(PunchBroker, RateIsCheckedBeforeAnythingIsLookedUp)
{
	// It protects us rather than informing the caller, so a flood is turned away before we do work
	// on its behalf.
	const PunchAsk ask( false, false, false, kMax );
	EXPECT_EQ(PunchVerdict::RateLimited, DecidePunch(ask, kMax));
}

TEST(PunchBroker, SupportIsAnsweredBeforeListing)
{
	// The commonest refusal comes first among the informative ones, so the answer that covers the
	// whole existing ecosystem is the cheapest to reach.
	const PunchAsk ask( false, false, false, 0 );
	EXPECT_EQ(PunchVerdict::NoSupport, DecidePunch(ask, kMax));
}

TEST(PunchBroker, ListingIsAnsweredBeforeTheCookie)
{
	const PunchAsk ask( false, true, false, 0 );
	EXPECT_EQ(PunchVerdict::NotListed, DecidePunch(ask, kMax));
}

TEST(PunchBroker, NoRefusalIsEverMistakenForConsent)
{
	// The property that matters more than any particular ordering: nothing but a fully good ask may
	// ever come back as Broker, because Broker is what causes a packet to be sent at somebody.
	for (int i = 0; i < 16; ++i)
	{
		const bool listed = (( i & 1 ) != 0 );
		const bool supports = (( i & 2 ) != 0 );
		const bool proven = (( i & 4 ) != 0 );
		const int recent = (( i & 8 ) != 0 ) ? kMax : 0;

		const PunchVerdict v = DecidePunch(PunchAsk( listed, supports, proven, recent ), kMax);
		const bool allGood = listed && supports && proven && ( recent < kMax );

		EXPECT_EQ(allGood, ( v == PunchVerdict::Broker )) << "case " << i;
	}
}

// ---------------------------------------------------------------- asking at all

TEST(PunchBroker, AsksForAListedServerWhileTheRegistryAnswers)
{
	EXPECT_EQ(PunchIntent::Ask, DecidePunchIntent( true, false, true ));
}

TEST(PunchBroker, SkipsOnTheLan)
{
	// Nothing crosses a router, so there is no hole to make.
	EXPECT_EQ(PunchIntent::Skip, DecidePunchIntent( true, true, true ));
}

TEST(PunchBroker, SkipsAPastedAddress)
{
	// No registry entry behind it, so the only possible answer is NotListed. Do not pay a round trip
	// to be told what we already know.
	EXPECT_EQ(PunchIntent::Skip, DecidePunchIntent( false, false, true ));
}

TEST(PunchBroker, SkipsWhileTheRegistryIsSilent)
{
	// The fallback would cover it, but paying a timeout on every join to rediscover that the
	// registry is still down is not a cost worth carrying.
	EXPECT_EQ(PunchIntent::Skip, DecidePunchIntent( true, false, false ));
}

TEST(PunchBroker, LanWinsOverEverything)
{
	EXPECT_EQ(PunchIntent::Skip, DecidePunchIntent( true, true, false ));
	EXPECT_EQ(PunchIntent::Skip, DecidePunchIntent( false, true, true ));
}

// ---------------------------------------------------------------- retrying

TEST(PunchBroker, TheFirstAttemptIsImmediate)
{
	EXPECT_EQ(0, PunchAttemptDelayMs(0));
}

TEST(PunchBroker, AttemptsBackOff)
{
	EXPECT_EQ(100, PunchAttemptDelayMs(1));
	EXPECT_EQ(300, PunchAttemptDelayMs(2));
	EXPECT_EQ(700, PunchAttemptDelayMs(3));
	EXPECT_EQ(1500, PunchAttemptDelayMs(4));
}

TEST(PunchBroker, EveryDelayIsLongerThanTheOneBefore)
{
	// One packet is not enough: the two sides act on separate messages and do not start together, so
	// the first punch can leave before the other end is listening. What matters is that the attempts
	// spread out rather than bunching.
	for (int i = 1; i < kPunchAttempts; ++i)
		EXPECT_GT(PunchAttemptDelayMs(i), PunchAttemptDelayMs(i - 1)) << "attempt " << i;
}

TEST(PunchBroker, OutOfRangeAttemptsAreClamped)
{
	EXPECT_EQ(0, PunchAttemptDelayMs(-1));
	EXPECT_EQ(PunchAttemptDelayMs(kPunchAttempts - 1), PunchAttemptDelayMs(kPunchAttempts));
	EXPECT_EQ(PunchAttemptDelayMs(kPunchAttempts - 1), PunchAttemptDelayMs(9999));
}

TEST(PunchBroker, EveryAttemptLandsInsideAJoinsPatience)
{
	// The whole sequence has to finish well inside the time somebody will wait for a server to
	// respond, or the fallback fires first and the punches were wasted.
	EXPECT_LT(PunchAttemptDelayMs(kPunchAttempts - 1), 3000);
}

TEST(PunchBroker, TheKeepaliveSitsInsideTheShortestMappingsSeenInTheWild)
{
	// NAT mappings commonly expire at 30 seconds and sometimes sooner, so refreshing must happen
	// comfortably inside that rather than near it.
	EXPECT_LT(kPunchKeepaliveMs, 30000);
	EXPECT_GT(kPunchKeepaliveMs, 0);
}
