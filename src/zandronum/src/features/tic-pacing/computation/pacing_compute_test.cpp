// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [Kaori-Chani] The pacing report's arithmetic, which is what a half-hour run and the CI job read.
// The case that matters most is the one a live run turned up: a sound clock with a loop that
// cannot keep up has to read as a loop fault, not as a wrong tic rate.

#include <gtest/gtest.h>

#include "features/tic-pacing/computation/pacing_compute.h"

using namespace zx::ticpacing;

TEST(TicPacing, AWindowIsDueOnceItsSecondsHavePassed)
{
	EXPECT_FALSE(ComputeDue(9999u, 10));
	EXPECT_TRUE(ComputeDue(10000u, 10));
	EXPECT_TRUE(ComputeDue(10001u, 10));
}

TEST(TicPacing, NoIntervalIsNeverDue)
{
	EXPECT_FALSE(ComputeDue(0u, 0));
	EXPECT_FALSE(ComputeDue(4000000000u, 0));
	EXPECT_FALSE(ComputeDue(4000000000u, -5));
}

// An interval of years must not multiply its way round to always due, which is what the
// comparison is widened for.
TEST(TicPacing, AHugeIntervalDoesNotOverflowIntoAlwaysDue)
{
	EXPECT_FALSE(ComputeDue(4294967295u, 2147483647));
}

// The measured case: 70 tics in 2000 ms, which is what a host reports when the time base and the
// loop are both keeping time.
TEST(TicPacing, ExactPacingReadsTheTicrate)
{
	const PacingReport r = ComputeReport(70u, 70u, 2000u);
	EXPECT_EQ(70u, r.runTics);
	EXPECT_EQ(70u, r.clockTics);
	EXPECT_EQ(2000u, r.ms);
	EXPECT_EQ(35000u, r.runMilliHz);
	EXPECT_EQ(0, r.behindTics);
}

// Half an hour has to come out exact rather than accumulating a rounding per tic, since that is
// the figure the live runs report.
TEST(TicPacing, AHalfHourOfExactPacingStaysExact)
{
	const PacingReport r = ComputeReport(35u * 1800u, 35u * 1800u, 1800000u);
	EXPECT_EQ(35000u, r.runMilliHz);
	EXPECT_EQ(0, r.behindTics);
}

// A loop that cannot keep up: its own rate falls away and the deficit counts it, which is what
// a client reads on a machine with no GPU and must not be mistaken for a timer fault.
TEST(TicPacing, AStalledLoopShowsInItsRateAndItsDeficit)
{
	const PacingReport r = ComputeReport(34u, 154u, 4400u);
	EXPECT_EQ(7727u, r.runMilliHz);
	EXPECT_EQ(120, r.behindTics);
}

// And the other way round, which a window cannot show at all: a window's tics and its
// milliseconds are one counter span scaled twice, so a counter at half rate reads here exactly
// like a correct one, and the logfile's stamps are what catch it (tools/tic_pacing_check.py).
TEST(TicPacing, AHalfRateCounterLooksExactlyLikeAGoodOne)
{
	const PacingReport r = ComputeReport(1050u, 1050u, 60000u);
	EXPECT_EQ(17500u, r.runMilliHz);
	EXPECT_EQ(0, r.behindTics);
}

TEST(TicPacing, ALoopAheadOfTheClockReadsAsASurplus)
{
	const PacingReport r = ComputeReport(36u, 35u, 1000u);
	EXPECT_EQ(36000u, r.runMilliHz);
	EXPECT_EQ(-1, r.behindTics);
}

TEST(TicPacing, AWindowOfNoMillisecondsHasNoRates)
{
	const PacingReport r = ComputeReport(7u, 9u, 0u);
	EXPECT_EQ(7u, r.runTics);
	EXPECT_EQ(9u, r.clockTics);
	EXPECT_EQ(0u, r.ms);
	EXPECT_EQ(0u, r.runMilliHz);
	// [Kaori-Chani] Still counted, since the tics are known even when no time has passed.
	EXPECT_EQ(2, r.behindTics);
}

// The rates are taken in 64 bits, so a window far longer than any run cannot wrap them, and the
// deficit is kept 64-bit because this one does not fit an int.
TEST(TicPacing, ALongWindowNeitherWrapsNorTruncates)
{
	const PacingReport r = ComputeReport(4000000u, 4000000000u, 4000000000u);
	EXPECT_EQ(1000u, r.runMilliHz);
	EXPECT_EQ(4000000000ll - 4000000ll, r.behindTics);
}
