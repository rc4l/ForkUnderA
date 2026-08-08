// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/federated-server-registry/computation/reachcookie_compute.h"

using namespace zx;

namespace
{

// The shipped limits, so the tests read as "what happens to a real request".
const int kPerSource = kMaxCookiesPerSource;
const int kTable = 256;

CookieVerdict Ask(bool sameSourceHasOne, int fromSameIP, int total)
{
	return DecideIssueCookie(sameSourceHasOne, fromSameIP, total, kPerSource, kTable);
}

} // namespace

TEST(ReachCookie, AFirstAskGetsOne)
{
	EXPECT_EQ(CookieVerdict::Issue, Ask(false, 0, 0));
}

TEST(ReachCookie, AskingAgainFromTheSameSourceHandsBackTheSameCookie)
{
	// The reply may simply have been dropped. Issuing a second one would invalidate nothing and cost
	// a slot, and refusing would punish somebody for the network losing a packet.
	EXPECT_EQ(CookieVerdict::Reissue, Ask(true, 1, 1));
}

TEST(ReachCookie, ARetryIsNotCountedAgainstTheAskersOwnLimit)
{
	// The bug this ordering prevents: a source at its limit retries, gets refused for hoarding, and
	// can never complete the probe it is retrying.
	EXPECT_EQ(CookieVerdict::Reissue, Ask(true, kPerSource, kTable / 2));
}

TEST(ReachCookie, ARetryStillWorksWhenTheTableIsCompletelyFull)
{
	// Their slot is already taken and no new one is needed, so a full table is not their problem.
	EXPECT_EQ(CookieVerdict::Reissue, Ask(true, 1, kTable));
}

TEST(ReachCookie, OneMachineCannotTakeMoreThanItsShare)
{
	// The whole point. Changing source port is free, so a limit keyed on address AND port is not a
	// limit at all; this one is keyed on the part an attacker cannot pick.
	EXPECT_EQ(CookieVerdict::Issue, Ask(false, kPerSource - 1, 0));
	EXPECT_EQ(CookieVerdict::TooManyFromSource, Ask(false, kPerSource, 0));
	EXPECT_EQ(CookieVerdict::TooManyFromSource, Ask(false, kPerSource + 500, 0));
}

TEST(ReachCookie, TwoServersBehindOneRouterBothGetOne)
{
	// A household running a couple of servers is ordinary and must not have to queue for a
	// ten-second cookie, which is why the per-source limit is a few rather than one.
	EXPECT_GT(kMaxCookiesPerSource, 1);
	EXPECT_EQ(CookieVerdict::Issue, Ask(false, 1, 5));
}

TEST(ReachCookie, AFullTableRefusesEverybodyEqually)
{
	EXPECT_EQ(CookieVerdict::Issue, Ask(false, 0, kTable - 1));
	EXPECT_EQ(CookieVerdict::TableFull, Ask(false, 0, kTable));
}

TEST(ReachCookie, AHoarderIsBlamedForHoardingEvenWhenTheTableIsFull)
{
	// Both limits are breached at once. Saying TooManyFromSource rather than TableFull is what makes
	// the log tell the truth about who filled it.
	EXPECT_EQ(CookieVerdict::TooManyFromSource, Ask(false, kPerSource, kTable));
}

TEST(ReachCookie, ANegativeLimitMeansNoLimit)
{
	// Only reachable by a caller passing one deliberately; pinned so that "off" is a decision rather
	// than something a limit of zero has to stand in for.
	EXPECT_EQ(CookieVerdict::Issue, DecideIssueCookie(false, 1000, 1000, -1, -1));
	EXPECT_EQ(CookieVerdict::TableFull, DecideIssueCookie(false, 1000, 1000, -1, 10));
	EXPECT_EQ(CookieVerdict::TooManyFromSource, DecideIssueCookie(false, 1000, 1000, 10, -1));
}

TEST(ReachCookie, ALimitOfZeroRefusesEverybody)
{
	EXPECT_EQ(CookieVerdict::TooManyFromSource, DecideIssueCookie(false, 0, 0, 0, kTable));
	EXPECT_EQ(CookieVerdict::TableFull, DecideIssueCookie(false, 0, 0, kPerSource, 0));
}

TEST(ReachCookie, TheTableIsBigEnoughToHoldSeveralHouseholdsWorth)
{
	// A cap that a couple of honest routers can exhaust is a denial of service with extra steps.
	EXPECT_GT(kTable, kMaxCookiesPerSource * 10);
}
