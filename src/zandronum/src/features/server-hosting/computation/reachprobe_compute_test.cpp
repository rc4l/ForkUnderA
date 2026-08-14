// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-hosting/computation/reachprobe_compute.h"

using zx::kCookieTimeoutMs;
using zx::kProbeCacheTtlMs;
using zx::kProbeTimeoutMs;
using zx::ProbeCacheKey;
using zx::ProbeCacheKeyMatches;
using zx::ProbeCacheUsable;
using zx::kFailedCacheTtlMs;
using zx::ProbeDisplay;
using zx::ProbeDisplayFor;
using zx::ProbeIsFinished;
using zx::ProbeNonceMatches;
using zx::ProbePhase;
using zx::ProbeSaysReachable;
using zx::StepProbe;

namespace
{
const bool kCookie = true;
const bool kProbe = true;
const bool kNothing = false;

ProbeCacheKey Key(const char *ip, const char *subnet, int port)
{
	ProbeCacheKey k;
	k.publicIp = ip;
	k.localSubnet = subnet;
	k.port = port;
	return k;
}
} // namespace

// ---------------------------------------------------------------- the two legs

TEST( ReachProbe, WaitsForTheCookieThenTheProbe )
{
	EXPECT_EQ( ProbePhase::AwaitingCookie,
		StepProbe( ProbePhase::AwaitingCookie, kNothing, kNothing, 100 ));

	EXPECT_EQ( ProbePhase::AwaitingProbe,
		StepProbe( ProbePhase::AwaitingCookie, kCookie, kNothing, 100 ));

	EXPECT_EQ( ProbePhase::Reachable,
		StepProbe( ProbePhase::AwaitingProbe, kNothing, kProbe, 100 ));
}

TEST( ReachProbe, KeepsWaitingWhileThereIsTimeLeft )
{
	// Both legs, unchanged, mid-wait. The ordinary case and the one easiest to leave untested,
	// because nothing happens in it.
	EXPECT_EQ( ProbePhase::AwaitingProbe,
		StepProbe( ProbePhase::AwaitingProbe, kNothing, kNothing, kProbeTimeoutMs - 1 ));

	EXPECT_EQ( ProbePhase::AwaitingCookie,
		StepProbe( ProbePhase::AwaitingCookie, kNothing, kNothing, kCookieTimeoutMs - 1 ));
}

TEST( ReachProbe, ANoShowOnEachLegMeansADIFFERENTThing )
{
	// [rc4l] The distinction the whole enum exists for. No cookie means the REGISTRY did not answer,
	// on a path that needs no forwarding -- so it says nothing about the player's router and must not
	// be reported as a closed port. No probe, having had a cookie, is the real negative.
	EXPECT_EQ( ProbePhase::Failed,
		StepProbe( ProbePhase::AwaitingCookie, kNothing, kNothing, kCookieTimeoutMs ));

	EXPECT_EQ( ProbePhase::Unreachable,
		StepProbe( ProbePhase::AwaitingProbe, kNothing, kNothing, kProbeTimeoutMs ));
}

TEST( ReachProbe, APacketOnTheDeadlineStillCounts )
{
	// Checked before the clock, so a probe in hand is never thrown away for being late. Failing a
	// player whose port is genuinely open is the worse error of the two.
	EXPECT_EQ( ProbePhase::Reachable,
		StepProbe( ProbePhase::AwaitingProbe, kNothing, kProbe, kProbeTimeoutMs ));

	EXPECT_EQ( ProbePhase::AwaitingProbe,
		StepProbe( ProbePhase::AwaitingCookie, kCookie, kNothing, kCookieTimeoutMs ));
}

TEST( ReachProbe, AVerdictIsNeverRewritten )
{
	// Retransmits and duplicates keep arriving after the answer. None may restart or flip it.
	const ProbePhase terminal[] = { ProbePhase::Reachable, ProbePhase::Unreachable,
		ProbePhase::Failed };

	for ( int i = 0; i < 3; ++i )
	{
		EXPECT_EQ( terminal[i], StepProbe( terminal[i], kCookie, kProbe, 0 )) << i;
		EXPECT_EQ( terminal[i], StepProbe( terminal[i], kNothing, kNothing, 999999 )) << i;
	}
}

TEST( ReachProbe, IdleDoesNotStartByItself )
{
	// A stray packet arriving when nothing was asked for must not conjure a probe out of nothing.
	EXPECT_EQ( ProbePhase::Idle, StepProbe( ProbePhase::Idle, kCookie, kProbe, 5000 ));
}

TEST( ReachProbe, ReportsWhichPhasesAreDone )
{
	EXPECT_FALSE( ProbeIsFinished( ProbePhase::Idle ));
	EXPECT_FALSE( ProbeIsFinished( ProbePhase::AwaitingCookie ));
	EXPECT_FALSE( ProbeIsFinished( ProbePhase::AwaitingProbe ));
	EXPECT_TRUE( ProbeIsFinished( ProbePhase::Reachable ));
	EXPECT_TRUE( ProbeIsFinished( ProbePhase::Unreachable ));
	EXPECT_TRUE( ProbeIsFinished( ProbePhase::Failed ));
}

TEST( ReachProbe, OnlyReachableIsAYes )
{
	// Failed is NOT a yes. An outage on our side must never green-light hosting we have not tested.
	EXPECT_TRUE( ProbeSaysReachable( ProbePhase::Reachable ));
	EXPECT_FALSE( ProbeSaysReachable( ProbePhase::Unreachable ));
	EXPECT_FALSE( ProbeSaysReachable( ProbePhase::Failed ));
	EXPECT_FALSE( ProbeSaysReachable( ProbePhase::Idle ));
	EXPECT_FALSE( ProbeSaysReachable( ProbePhase::AwaitingCookie ));
	EXPECT_FALSE( ProbeSaysReachable( ProbePhase::AwaitingProbe ));
}

// ---------------------------------------------------------------- the nonce

TEST( ReachProbe, OnlyOurOwnNonceCounts )
{
	// [rc4l] THE FALSE-POSITIVE GUARD. The socket is open to the internet for a couple of seconds and
	// anyone may send it something; "a packet arrived" is not the question being asked.
	EXPECT_TRUE( ProbeNonceMatches( "abc123", "abc123" ));
	EXPECT_FALSE( ProbeNonceMatches( "abc123", "abc124" ));
	EXPECT_FALSE( ProbeNonceMatches( "abc123", "" ));
}

TEST( ReachProbe, AnEmptyExpectationMatchesNothing )
{
	// A client that failed to generate a nonce would otherwise accept everything -- turning the guard
	// into its opposite exactly when it is broken.
	EXPECT_FALSE( ProbeNonceMatches( "", "" ));
	EXPECT_FALSE( ProbeNonceMatches( "", "anything" ));
}

// ---------------------------------------------------------------- the cache

TEST( ReachProbe, ADifferentPortIsADifferentQuestion )
{
	// [rc4l] Forwards are per-port. 10666 being open says nothing whatever about 10667, so changing
	// the preferred port must miss the cache rather than reuse an answer about another port.
	EXPECT_FALSE( ProbeCacheKeyMatches( Key( "1.2.3.4", "192.168.1", 10666 ),
		Key( "1.2.3.4", "192.168.1", 10667 )));
}

TEST( ReachProbe, ADifferentNetworkIsADifferentQuestion )
{
	// A laptop that moved, or an ISP that moved the player. Either invalidates the answer.
	EXPECT_FALSE( ProbeCacheKeyMatches( Key( "1.2.3.4", "192.168.1", 10666 ),
		Key( "9.9.9.9", "192.168.1", 10666 )));

	EXPECT_FALSE( ProbeCacheKeyMatches( Key( "1.2.3.4", "192.168.1", 10666 ),
		Key( "1.2.3.4", "10.0.0", 10666 )));
}

TEST( ReachProbe, TheSameQuestionMatches )
{
	EXPECT_TRUE( ProbeCacheKeyMatches( Key( "1.2.3.4", "192.168.1", 10666 ),
		Key( "1.2.3.4", "192.168.1", 10666 )));
}

TEST( ReachProbe, AFreshAnswerForTheSameQuestionIsUsable )
{
	EXPECT_TRUE( ProbeCacheUsable( Key( "1.2.3.4", "192.168.1", 10666 ),
		Key( "1.2.3.4", "192.168.1", 10666 ), 1000, ProbePhase::Reachable ));
}

TEST( ReachProbe, AnExpiredAnswerIsNot )
{
	EXPECT_FALSE( ProbeCacheUsable( Key( "1.2.3.4", "192.168.1", 10666 ),
		Key( "1.2.3.4", "192.168.1", 10666 ), kProbeCacheTtlMs, ProbePhase::Reachable ));
}

TEST( ReachProbe, ANegativeAgeIsNeverUsable )
{
	// [rc4l] Clocks move backwards -- an NTP resync, a laptop waking. A negative age would sail past
	// a "less than the TTL" test and keep a stale verdict alive for as long as the clock stayed wrong.
	EXPECT_FALSE( ProbeCacheUsable( Key( "1.2.3.4", "192.168.1", 10666 ),
		Key( "1.2.3.4", "192.168.1", 10666 ), -1, ProbePhase::Reachable ));
}

TEST( ReachProbe, AFreshAnswerToADIFFERENTQuestionIsStillUnusable )
{
	// Freshness does not rescue a key mismatch; both have to hold.
	EXPECT_FALSE( ProbeCacheUsable( Key( "1.2.3.4", "192.168.1", 10666 ),
		Key( "1.2.3.4", "192.168.1", 10667 ), 0, ProbePhase::Reachable ));
}

// [rc4l] Failed is not a verdict, it is the absence of one, so it expires on its own short clock.
// Holding it for the full TTL painted the INTERNET option white for ten minutes over a single
// request the registry's flood queue happened to swallow.
TEST( ReachProbe, AFailedAnswerExpiresOnTheShortClock )
{
	EXPECT_FALSE( ProbeCacheUsable( Key( "1.2.3.4", "192.168.1", 10666 ),
		Key( "1.2.3.4", "192.168.1", 10666 ), kFailedCacheTtlMs, ProbePhase::Failed ));
}

TEST( ReachProbe, AFailedAnswerIsStillUsedWhileItIsFresh )
{
	// Briefly, so a registry that is genuinely down is re-asked every few seconds rather than every
	// frame.
	EXPECT_TRUE( ProbeCacheUsable( Key( "1.2.3.4", "192.168.1", 10666 ),
		Key( "1.2.3.4", "192.168.1", 10666 ), kFailedCacheTtlMs - 1, ProbePhase::Failed ));
}

TEST( ReachProbe, ARealVerdictOutlivesTheFailedClock )
{
	// The short clock is for Failed alone. Unreachable is an answer and keeps the full TTL.
	EXPECT_TRUE( ProbeCacheUsable( Key( "1.2.3.4", "192.168.1", 10666 ),
		Key( "1.2.3.4", "192.168.1", 10666 ), kFailedCacheTtlMs, ProbePhase::Unreachable ));
}

// ---------------------------------------------------------------- what the INTERNET option shows

TEST( ReachProbe, OnlyAnArrivedProbeReadsAsReachable )
{
	EXPECT_EQ( ProbeDisplay::Reachable, ProbeDisplayFor( ProbePhase::Reachable ));
}

TEST( ReachProbe, ACompletedCheckThatFoundNothingReadsAsUnreachable )
{
	EXPECT_EQ( ProbeDisplay::Unreachable, ProbeDisplayFor( ProbePhase::Unreachable ));
}

TEST( ReachProbe, AFailedCheckIsNotTheSameAsAClosedPort )
{
	// [rc4l] The one that matters. Failed means the REGISTRY never answered, so it says nothing about
	// the player's router -- showing it as unreachable would blame them for our outage, and would do
	// it at exactly the moment our service is already broken.
	EXPECT_EQ( ProbeDisplay::Unknown, ProbeDisplayFor( ProbePhase::Failed ));
	EXPECT_NE( ProbeDisplay::Unreachable, ProbeDisplayFor( ProbePhase::Failed ));
}

TEST( ReachProbe, NothingIsClaimedBeforeTheCheckHasFinished )
{
	// Untested and mid-flight both mean "we do not know", and neither may be drawn as an answer.
	EXPECT_EQ( ProbeDisplay::Unknown, ProbeDisplayFor( ProbePhase::Idle ));
	EXPECT_EQ( ProbeDisplay::Unknown, ProbeDisplayFor( ProbePhase::AwaitingCookie ));
	EXPECT_EQ( ProbeDisplay::Unknown, ProbeDisplayFor( ProbePhase::AwaitingProbe ));
}
