// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frameclock/computation/frameclock_compute.h"

#include <gtest/gtest.h>

using namespace zx::frameclock;

TEST(FrameClock, ACounterReadsInMicrosecondsWithoutOverflowing)
{
	EXPECT_EQ(1500000LL, ComputeCounterMicros(15000000LL, 10000000LL));
	EXPECT_EQ(3LL, ComputeCounterMicros(30LL, 10000000LL));
	// [rc4l] Thirty days of a 10 MHz counter, which multiplied by a million first would overflow.
	EXPECT_EQ(2592000000000LL, ComputeCounterMicros(25920000000000LL, 10000000LL));
	EXPECT_EQ(0LL, ComputeCounterMicros(5LL, 0LL));
}

TEST(FrameClock, TimeSinceAMillisecondKeepsTheMicroseconds)
{
	EXPECT_EQ(2250LL, ComputeSinceMsUs(102250LL, 100u));
	EXPECT_EQ(-750LL, ComputeSinceMsUs(102250LL, 103u));
}

// [rc4l] The millisecond clock wraps every 49.7 days, as timeGetTime did.
TEST(FrameClock, TimeSinceAMillisecondCrossesTheWrap)
{
	const long long nowUs = 4294967296LL * 1000 + 1500;   // [rc4l] 1.5 ms past the wrap
	EXPECT_EQ(3500LL, ComputeSinceMsUs(nowUs, 0xFFFFFFFEu));
}

TEST(FrameClock, TheTicFractionIsMicrosecondExact)
{
	EXPECT_EQ(0, ComputeTicFrac(0, 28000));
	EXPECT_EQ(32768, ComputeTicFrac(14000, 28000));
	EXPECT_EQ(2340, ComputeTicFrac(1000, 28000));
	EXPECT_EQ(2342, ComputeTicFrac(1001, 28000));
}

// [rc4l] fua_debug_ticfrac_ms: the old clock's whole milliseconds, for an A/B against the microsecond fraction.
TEST(FrameClock, WholeMillisecondsAreWhatAMillisecondClockCounted)
{
	EXPECT_EQ(2000, ComputeWholeMsUs(2999));
	EXPECT_EQ(3000, ComputeWholeMsUs(3000));
	EXPECT_EQ(0, ComputeWholeMsUs(0));
	EXPECT_EQ(0, ComputeWholeMsUs(-500));
	EXPECT_EQ(ComputeTicFrac(1000, 28000), ComputeTicFrac(ComputeWholeMsUs(1999), 28000));
}

TEST(FrameClock, TheTicFractionStaysWithinTheTic)
{
	EXPECT_EQ(0, ComputeTicFrac(-300, 28000));
	EXPECT_EQ(65536, ComputeTicFrac(28000, 28000));
	EXPECT_EQ(65536, ComputeTicFrac(40000, 28000));
	EXPECT_EQ(65536, ComputeTicFrac(10, 0));
}
