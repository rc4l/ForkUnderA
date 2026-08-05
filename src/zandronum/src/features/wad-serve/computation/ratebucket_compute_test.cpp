// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/wad-serve/computation/ratebucket_compute.h"

using zx::BucketAvailable;
using zx::BucketCapacity;
using zx::BucketCharge;
using zx::BucketTake;
using zx::BucketTakePair;
using zx::BucketWaitMs;
using zx::BucketWaitMsPair;
using zx::kMaxWaitMs;
using zx::kUnlimitedBytes;
using zx::RateBucket;
using zx::RateLimit;

namespace
{
// 1000 bytes a second with a one-second burst, so a millisecond is exactly one byte and every
// expectation below can be written as a round number.
RateLimit Simple() { return RateLimit(1000, 1000); }
} // namespace

TEST(BucketCapacity, IsTheBurstWhenOneIsConfigured)
{
	EXPECT_EQ(4096LL, BucketCapacity(RateLimit(1000, 4096)));
}

TEST(BucketCapacity, FallsBackToOneSecondOfRate)
{
	// The useful default: start at full speed on an idle server, but never bank minutes of unused
	// allowance and dump it in a spike.
	EXPECT_EQ(1000LL, BucketCapacity(RateLimit(1000, 0)));
}

TEST(BucketCapacity, IsUnlimitedWhenTheRateIsOff)
{
	EXPECT_EQ(kUnlimitedBytes, BucketCapacity(RateLimit(0, 0)));
	EXPECT_EQ(kUnlimitedBytes, BucketCapacity(RateLimit(-5, 0)));
}

TEST(BucketAvailable, AnUnprimedBucketStartsFull)
{
	RateBucket b;
	EXPECT_EQ(1000LL, BucketAvailable(b, Simple(), 50000));
	EXPECT_TRUE(b.primed);
	EXPECT_EQ(50000LL, b.lastMs);
}

TEST(BucketAvailable, TokensAccrueWithElapsedTime)
{
	RateBucket b;
	const RateLimit limit = Simple();

	BucketTake(b, limit, 1000, 0);					// drain it
	EXPECT_EQ(0LL, BucketAvailable(b, limit, 0));

	EXPECT_EQ(250LL, BucketAvailable(b, limit, 250));
	EXPECT_EQ(600LL, BucketAvailable(b, limit, 600));
}

TEST(BucketAvailable, TokensNeverExceedTheBurstCeiling)
{
	RateBucket b;
	const RateLimit limit = Simple();

	BucketTake(b, limit, 1000, 0);
	// An hour of idling must not bank an hour of bandwidth.
	EXPECT_EQ(1000LL, BucketAvailable(b, limit, 3600000));
}

TEST(BucketAvailable, IsUnlimitedWhenTheRateIsOff)
{
	RateBucket b;
	EXPECT_EQ(kUnlimitedBytes, BucketAvailable(b, RateLimit(0, 0), 0));
	EXPECT_FALSE(b.primed) << "an unlimited bucket should not need priming";
}

TEST(BucketAvailable, AClockThatMovesBackwardsResynchronises)
{
	// Not hypothetical enough to ignore: this runs for months on machines whose clocks get stepped.
	// Granting nothing is right; stalling until the old stamp comes round again is not.
	RateBucket b;
	const RateLimit limit = Simple();

	BucketTake(b, limit, 1000, 100000);
	EXPECT_EQ(0LL, BucketAvailable(b, limit, 100000));

	EXPECT_EQ(0LL, BucketAvailable(b, limit, 40000)) << "a backwards jump must not grant tokens";
	EXPECT_EQ(40000LL, b.lastMs) << "it must resync, or the bucket stalls for a minute";
	EXPECT_EQ(500LL, BucketAvailable(b, limit, 40500)) << "and refill normally from there";
}

TEST(BucketCharge, IsANoOpWhenUnlimited)
{
	RateBucket b;
	BucketCharge(b, RateLimit(0, 0), 5000);
	EXPECT_DOUBLE_EQ(0.0, b.tokens);
}

TEST(BucketCharge, IgnoresANonPositiveAmount)
{
	RateBucket b;
	const RateLimit limit = Simple();
	BucketAvailable(b, limit, 0);

	BucketCharge(b, limit, 0);
	BucketCharge(b, limit, -100);
	EXPECT_EQ(1000LL, BucketAvailable(b, limit, 0));
}

TEST(BucketCharge, NeverDrivesTokensNegative)
{
	// An overcharge should cost one refill interval, not bank a debt that keeps the connection
	// silent for hours.
	RateBucket b;
	const RateLimit limit = Simple();
	BucketAvailable(b, limit, 0);

	BucketCharge(b, limit, 999999);
	EXPECT_DOUBLE_EQ(0.0, b.tokens);
	EXPECT_EQ(100LL, BucketAvailable(b, limit, 100));
}

TEST(BucketTake, GrantsTheWholeRequestWhenTokensAllow)
{
	RateBucket b;
	EXPECT_EQ(400LL, BucketTake(b, Simple(), 400, 0));
}

TEST(BucketTake, IsLimitedByAvailableTokens)
{
	RateBucket b;
	const RateLimit limit = Simple();
	EXPECT_EQ(1000LL, BucketTake(b, limit, 5000, 0));
	EXPECT_EQ(0LL, BucketTake(b, limit, 5000, 0));
}

TEST(BucketTake, GrantsNothingForANonPositiveRequest)
{
	RateBucket b;
	EXPECT_EQ(0LL, BucketTake(b, Simple(), 0, 0));
	EXPECT_EQ(0LL, BucketTake(b, Simple(), -1, 0));
}

TEST(BucketTake, GrantsEverythingWhenTheLimitIsOff)
{
	RateBucket b;
	EXPECT_EQ(999999LL, BucketTake(b, RateLimit(0, 0), 999999, 0));
}

TEST(BucketTakePair, ChargesBothOnlyForTheSmallerGrant)
{
	// The bug this function exists to prevent. Charging the global budget first and then discovering
	// the connection cap is smaller spends server-wide allowance on bytes nobody received, throttling
	// every other downloader to pay for data that never moved.
	RateBucket global, conn;
	const RateLimit globalLimit(1000000, 1000000);
	const RateLimit connLimit(1000, 1000);

	EXPECT_EQ(1000LL, BucketTakePair(global, globalLimit, conn, connLimit, 100000, 0));

	EXPECT_EQ(999000LL, BucketAvailable(global, globalLimit, 0))
		<< "the global budget was charged for bytes the connection cap refused";
	EXPECT_EQ(0LL, BucketAvailable(conn, connLimit, 0));
}

TEST(BucketTakePair, IsLimitedByTheGlobalBudget)
{
	RateBucket global, conn;
	const RateLimit globalLimit(500, 500);
	const RateLimit connLimit(100000, 100000);

	EXPECT_EQ(500LL, BucketTakePair(global, globalLimit, conn, connLimit, 4096, 0));
	EXPECT_EQ(99500LL, BucketAvailable(conn, connLimit, 0));
}

TEST(BucketTakePair, GrantsTheRequestWhenNeitherLimitBinds)
{
	RateBucket global, conn;
	EXPECT_EQ(64LL, BucketTakePair(global, RateLimit(1000000, 1000000), conn, Simple(), 64, 0));
}

TEST(BucketTakePair, GrantsNothingForANonPositiveRequest)
{
	RateBucket global, conn;
	EXPECT_EQ(0LL, BucketTakePair(global, Simple(), conn, Simple(), 0, 0));
}

TEST(BucketWait, IsZeroWhenTokensAlreadyCover)
{
	RateBucket b;
	const RateLimit limit = Simple();
	BucketAvailable(b, limit, 0);
	EXPECT_EQ(0, BucketWaitMs(b, limit, 500));
}

TEST(BucketWait, ScalesWithTheDeficitAndRoundsUp)
{
	RateBucket b;
	const RateLimit limit = Simple();
	BucketTake(b, limit, 1000, 0);					// empty

	// 100 bytes at 1000 B/s is 100 ms; rounded up so a short sleep does not just spin.
	EXPECT_EQ(101, BucketWaitMs(b, limit, 100));
}

TEST(BucketWait, IsCappedSoTheWorkerStaysResponsive)
{
	// Without the cap a heavily throttled transfer sleeps through a shutdown request.
	RateBucket b;
	const RateLimit limit = Simple();
	BucketTake(b, limit, 1000, 0);
	EXPECT_EQ(kMaxWaitMs, BucketWaitMs(b, limit, 1000000));
}

TEST(BucketWait, IsZeroWhenTheLimitIsOff)
{
	RateBucket b;
	EXPECT_EQ(0, BucketWaitMs(b, RateLimit(0, 0), 999999));
}

TEST(BucketWait, IsZeroForANonPositiveRequest)
{
	RateBucket b;
	EXPECT_EQ(0, BucketWaitMs(b, Simple(), 0));
}

TEST(BucketWaitPair, TakesTheLongerOfTheTwo)
{
	RateBucket slow, fast;
	const RateLimit slowLimit(100, 100);
	const RateLimit fastLimit(100000, 100000);
	BucketTake(slow, slowLimit, 100, 0);			// empty the slow one
	BucketAvailable(fast, fastLimit, 0);			// fill the fast one

	EXPECT_EQ(101, BucketWaitMsPair(slow, slowLimit, fast, fastLimit, 10));
	EXPECT_EQ(101, BucketWaitMsPair(fast, fastLimit, slow, slowLimit, 10))
		<< "argument order must not change the answer";
}

TEST(RateBucket, TwentyClientsShareTheGlobalBudgetRatherThanMultiplyingIt)
{
	// The scenario the defaults were chosen against: everyone joining at a map change. Per-connection
	// caps alone would let this reach 20x the intended rate -- the global bucket is what holds it to
	// the configured ceiling, which is the entire reason it exists.
	const RateLimit globalLimit(512 * 1024, 512 * 1024);
	const RateLimit connLimit(256 * 1024, 256 * 1024);

	RateBucket global;
	RateBucket conn[20];

	long long granted = 0;
	for (int i = 0; i < 20; ++i)
		granted += BucketTakePair(global, globalLimit, conn[i], connLimit, 64 * 1024, 0);

	EXPECT_EQ(512LL * 1024, granted) << "twenty clients must not get twenty times the budget";

	// And a second later, exactly one more second of budget -- not twenty.
	long long second = 0;
	for (int i = 0; i < 20; ++i)
		second += BucketTakePair(global, globalLimit, conn[i], connLimit, 64 * 1024, 1000);

	EXPECT_EQ(512LL * 1024, second);
}
