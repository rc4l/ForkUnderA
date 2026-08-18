// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/querypunch_compute.h"

using zx::QueryPunchStep;
using zx::StepQueryPunch;

namespace
{
int CountSet(const QueryPunchStep &s)
{
	return (s.requestPunch ? 1 : 0) + (s.resendChallenge ? 1 : 0) + (s.timeOut ? 1 : 0);
}
} // namespace

TEST(StepQueryPunch, AnIneligibleSlotKeepsThePrePunchLifetimeExactly)
{
	// [rc4l] The regression that must never happen: this feature exists for CGNAT servers, and a
	// LAN server or a budget-starved slot must behave to the millisecond as the browser always did.
	EXPECT_EQ(0, CountSet(StepQueryPunch(0, false, false, 0)));
	EXPECT_EQ(0, CountSet(StepQueryPunch(3999, false, false, 0)));
	EXPECT_TRUE(StepQueryPunch(4000, false, false, 0).timeOut);
}

TEST(StepQueryPunch, AsksForThePunchOnceThePlainQueryHasClearlyMissed)
{
	// Not instantly -- most servers answer in well under a second, and punching every listed
	// server on every refresh would drain the registry's rate limit for nothing.
	EXPECT_EQ(0, CountSet(StepQueryPunch(1499, true, false, 0)));
	EXPECT_TRUE(StepQueryPunch(1500, true, false, 0).requestPunch);
	EXPECT_TRUE(StepQueryPunch(3000, true, false, 0).requestPunch);
}

TEST(StepQueryPunch, ABudgetStarvedEligibleSlotStillDiesAtThePlainTimeout)
{
	// punchEligible but the caller never granted it (kept punchRequested false): at 4000 the slot
	// times out rather than asking forever.
	EXPECT_TRUE(StepQueryPunch(4000, true, false, 0).timeOut);
	EXPECT_FALSE(StepQueryPunch(4000, true, false, 0).requestPunch);
}

TEST(StepQueryPunch, WalksTheResendLadderBehindThePunch)
{
	// The resends trail the server's punch packet schedule so one challenge lands in the hole.
	EXPECT_EQ(0, CountSet(StepQueryPunch(2499, true, true, 0)));
	EXPECT_TRUE(StepQueryPunch(2500, true, true, 0).resendChallenge);
	EXPECT_EQ(0, CountSet(StepQueryPunch(2600, true, true, 1)));
	EXPECT_TRUE(StepQueryPunch(4000, true, true, 1).resendChallenge);
	EXPECT_TRUE(StepQueryPunch(5500, true, true, 2).resendChallenge);
	// Ladder exhausted: nothing more to send, not timed out yet either.
	EXPECT_EQ(0, CountSet(StepQueryPunch(5600, true, true, 3)));
}

TEST(StepQueryPunch, APunchedSlotGetsTheExtendedTimeoutAndNoMore)
{
	EXPECT_EQ(0, CountSet(StepQueryPunch(6999, true, true, 3)));
	EXPECT_TRUE(StepQueryPunch(7000, true, true, 3).timeOut);
	// Timeout wins over a straggling resend threshold.
	EXPECT_TRUE(StepQueryPunch(7000, true, true, 0).timeOut);
	EXPECT_FALSE(StepQueryPunch(7000, true, true, 0).resendChallenge);
}

TEST(StepQueryPunch, NegativeElapsedDoesNothing)
{
	// lMSTime can be unstamped or the clock can step; a nonsense elapsed must not punch or kill.
	EXPECT_EQ(0, CountSet(StepQueryPunch(-1, true, false, 0)));
	EXPECT_EQ(0, CountSet(StepQueryPunch(-1, true, true, 2)));
}

TEST(StepQueryPunch, AtMostOneActionPerStep)
{
	for (int t = 0; t <= 8000; t += 100)
	{
		for (int eligible = 0; eligible < 2; ++eligible)
			for (int requested = 0; requested < 2; ++requested)
				for (int resends = 0; resends <= 3; ++resends)
					EXPECT_LE(CountSet(StepQueryPunch(t, eligible != 0, requested != 0, resends)), 1);
	}
}

//*****************************************************************************
// ShouldAdoptPunchKnock -- the re-aim rule, which is also the security boundary.

TEST(ShouldAdoptPunchKnock, OnlyAWaitingInvitedMatchingSlotMayBeReaimed)
{
	EXPECT_TRUE(zx::ShouldAdoptPunchKnock(true, true, true));
}

TEST(ShouldAdoptPunchKnock, AnUninvitedSlotCanNeverBeRedirected)
{
	// [rc4l] The security property: a spoofed knock against a row that never asked for a punch
	// must do nothing -- otherwise any packet source could steer where a listed row points.
	EXPECT_FALSE(zx::ShouldAdoptPunchKnock(true, false, true));
}

TEST(ShouldAdoptPunchKnock, ASettledSlotIsLeftAlone)
{
	// Answered rows carry live data a player may be about to join through; a late knock must not
	// rewrite their address underneath them.
	EXPECT_FALSE(zx::ShouldAdoptPunchKnock(false, true, true));
}

TEST(ShouldAdoptPunchKnock, AKnockFromSomeOtherHostMatchesNothing)
{
	EXPECT_FALSE(zx::ShouldAdoptPunchKnock(true, true, false));
	// And no combination of the other flags rescues a host mismatch.
	EXPECT_FALSE(zx::ShouldAdoptPunchKnock(false, false, false));
	EXPECT_FALSE(zx::ShouldAdoptPunchKnock(true, false, false));
	EXPECT_FALSE(zx::ShouldAdoptPunchKnock(false, true, false));
}

//*****************************************************************************
// ShouldRearmListedSlot -- a re-announced server must be able to return.

TEST(ShouldRearmListedSlot, ATimedOutSlotRearmsWhenTheRegistryStillListsIt)
{
	// [rc4l] The regression this pins: one missed reply window used to remove a server for the
	// whole session, because the add-path dedupe ate every later re-announcement of its address.
	EXPECT_TRUE(zx::ShouldRearmListedSlot(true, false, false));
	// Even mid-recheck bookkeeping on a timed-out slot does not block the re-arm.
	EXPECT_TRUE(zx::ShouldRearmListedSlot(true, false, true));
}

TEST(ShouldRearmListedSlot, ARemovedSlotRearmsUnlessARecheckIsStillFlying)
{
	EXPECT_TRUE(zx::ShouldRearmListedSlot(false, true, false));
	// A re-check in flight will settle the slot itself; re-arming under it would race the answer.
	EXPECT_FALSE(zx::ShouldRearmListedSlot(false, true, true));
}

TEST(ShouldRearmListedSlot, LiveAndAnsweredSlotsAreTrueDuplicates)
{
	// Neither timed out nor inactive: the slot is live (active, or mid-query) -- the announcement
	// is a genuine duplicate and must change nothing.
	EXPECT_FALSE(zx::ShouldRearmListedSlot(false, false, false));
	EXPECT_FALSE(zx::ShouldRearmListedSlot(false, false, true));
}

// --- punching before the first challenge ----------------------------------------------------------
//
// The joiner's first challenge lands on the host's router as unsolicited traffic, and the router
// tracks it even while dropping it -- taking the very tuple the host's punch then needs. Verified
// with conntrack in the NAT lab: the punch left on a rewritten source port and the hole opened where
// nobody was knocking.

TEST(QueryPunchLead, AKnownUnreachableInternetServerPunchesFirst)
{
	EXPECT_TRUE( zx::ShouldPunchBeforeFirstChallenge( false, true, true ));
}

TEST(QueryPunchLead, AServerThatHasNeverBeenTriedJustAsks)
{
	// Most listed servers answer.
	EXPECT_FALSE( zx::ShouldPunchBeforeFirstChallenge( false, false, true ));
}

TEST(QueryPunchLead, ALanServerNeverPunchesFirst)
{
	// No router is crossed, so there is no hole to open.
	EXPECT_FALSE( zx::ShouldPunchBeforeFirstChallenge( true, true, true ));
}

TEST(QueryPunchLead, NoBudgetMeansNoLead)
{
	// Without the ask actually going out, holding the challenge back would only delay the row.
	EXPECT_FALSE( zx::ShouldPunchBeforeFirstChallenge( false, true, false ));
}

TEST(QueryPunchLead, TheHeldChallengeWaitsForTheLeadThenGoes)
{
	EXPECT_FALSE( zx::FirstChallengeDue( true, false, 0 ));
	EXPECT_FALSE( zx::FirstChallengeDue( true, false, zx::kQueryPunchLeadMs - 1 ));
	EXPECT_TRUE( zx::FirstChallengeDue( true, false, zx::kQueryPunchLeadMs ));
	EXPECT_TRUE( zx::FirstChallengeDue( true, false, zx::kQueryPunchLeadMs + 5000 ));
}

TEST(QueryPunchLead, ARowThatDidNotLeadIsNeverHeld)
{
	// Its challenge already went out with the sweep; there is nothing being waited for.
	EXPECT_FALSE( zx::FirstChallengeDue( false, false, 10000 ));
}

TEST(QueryPunchLead, TheChallengeIsSentOnlyOnce)
{
	EXPECT_FALSE( zx::FirstChallengeDue( true, true, 10000 ));
}

TEST(QueryPunchLead, FirstContactWithAnInternetServerLeadsWhenBudgetRemains)
{
	// Leading on a later sweep is too late: the entry the first challenge created is refreshed by
	// every retry, so the tuple stays taken for as long as the joiner keeps talking.
	EXPECT_TRUE( zx::ShouldPunchOnFirstContact( false, true ));
}

TEST(QueryPunchLead, FirstContactOnTheLanStillNeverPunches)
{
	EXPECT_FALSE( zx::ShouldPunchOnFirstContact( true, true ));
}

TEST(QueryPunchLead, FirstContactYieldsWhenTheBudgetIsGone)
{
	// The known-unreachable rows take theirs first; these are the speculative ones.
	EXPECT_FALSE( zx::ShouldPunchOnFirstContact( false, false ));
}
