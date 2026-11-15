//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: i_tictime_test.cpp
//
// Description: The tic time base's arithmetic. This is the only coverage the
// time base gets on a platform nobody can run by hand: it is built into the
// portable test target, so it runs on Linux, macOS, Windows and wasm32 -- the
// last being the only 32-bit target in the matrix, and a browser the only
// place the engine's pacing cannot be measured live.
//
// The two frequency-parameterized cores take their frequency as an argument,
// so every case here is exact on every platform rather than depending on the
// machine's clock. The two that read the clock are checked for the properties
// a caller relies on: monotonic, and a frequency that clears TICRATE.
//
//-----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include "i_tictime.h"
#include "doomdef.h"	// TICRATE

namespace
{

// The frequencies a caller can meet: the boundary the round trip needs, one
// past it, and the real ones from the platforms this runs on.
const tictime_raw_t FREQS[] = {
	(tictime_raw_t)TICRATE,			// the exact boundary
	(tictime_raw_t)TICRATE + 1,
	1000ull,
	3579545ull,						// the legacy PC timer rate
	10000000ull,					// a typical QueryPerformanceFrequency
	1000000000ull,					// clock_gettime(CLOCK_MONOTONIC)
};

const tictime_raw_t TICS[] = {
	0ull, 1ull, 34ull, 35ull, 36ull, 1000ull, 12345ull,
	(tictime_raw_t)TICRATE * 3600ull * 24ull,			// one day
	(tictime_raw_t)TICRATE * 3600ull * 24ull * 365ull,	// one year
};

}

// A tic converted to raw counts and back is the tic it started as. This is
// the property the whole time base rests on: a tic boundary is a tic
// boundary at any frequency, so no tic is ever lost to rounding.
TEST(TicTime, ATicSurvivesTheRoundTripAtEveryFrequency)
{
	for (unsigned int i = 0; i < sizeof(FREQS) / sizeof(FREQS[0]); ++i)
	{
		for (unsigned int j = 0; j < sizeof(TICS) / sizeof(TICS[0]); ++j)
		{
			const tictime_raw_t raw = TIC_TicsToRawWith(TICS[j], FREQS[i]);
			EXPECT_EQ(TICS[j], TIC_ScaleRawWith(raw, TICRATE, FREQS[i]))
				<< "freq " << FREQS[i] << " tic " << TICS[j];
		}
	}
}

// And the boundary is exact on BOTH sides: one count before a tic's start
// must still read as the tic before it. Without this a caller waiting for
// the next tic could see it twice, or never.
TEST(TicTime, TheCountBeforeATicIsStillThePreviousTic)
{
	for (unsigned int i = 0; i < sizeof(FREQS) / sizeof(FREQS[0]); ++i)
	{
		for (unsigned int j = 0; j < sizeof(TICS) / sizeof(TICS[0]); ++j)
		{
			if (TICS[j] == 0)
				continue;
			const tictime_raw_t raw = TIC_TicsToRawWith(TICS[j], FREQS[i]);
			EXPECT_EQ(TICS[j] - 1, TIC_ScaleRawWith(raw - 1, TICRATE, FREQS[i]))
				<< "freq " << FREQS[i] << " tic " << TICS[j];
		}
	}
}

// Scaling is monotonic in the raw count: time never goes backwards for a
// caller that reads a larger counter.
TEST(TicTime, ScalingNeverGoesBackwards)
{
	const tictime_raw_t freq = 10000000ull;
	tictime_raw_t last = 0;
	for (tictime_raw_t raw = 0; raw < freq * 3ull; raw += freq / 1000ull)
	{
		const tictime_raw_t ms = TIC_ScaleRawWith(raw, 1000, freq);
		EXPECT_GE(ms, last) << "raw " << raw;
		last = ms;
	}
}

// Whole seconds and whole milliseconds come out exact, which is what the
// pacing measurement reports and what a reader checks by hand.
TEST(TicTime, WholeSecondsAndMillisecondsAreExact)
{
	for (unsigned int i = 0; i < sizeof(FREQS) / sizeof(FREQS[0]); ++i)
	{
		const tictime_raw_t freq = FREQS[i];
		for (tictime_raw_t sec = 0; sec <= 90ull; sec += 30ull)
		{
			EXPECT_EQ(sec * 1000ull, TIC_ScaleRawWith(freq * sec, 1000, freq));
			EXPECT_EQ(sec * (tictime_raw_t)TICRATE,
				TIC_ScaleRawWith(freq * sec, TICRATE, freq));
		}
	}
}

// The split division is what keeps the arithmetic exact at a long uptime
// rather than overflowing: a year at a nanosecond clock is about 3.2e16
// counts, and scaling that by 1000 would overflow a naive raw*mul.
TEST(TicTime, ALongUptimeScalesWithoutOverflowing)
{
	const tictime_raw_t freq = 1000000000ull;			// nanoseconds
	const tictime_raw_t year = freq * 3600ull * 24ull * 365ull;
	EXPECT_EQ(3600ull * 24ull * 365ull * 1000ull, TIC_ScaleRawWith(year, 1000, freq));
	EXPECT_EQ(3600ull * 24ull * 365ull * (tictime_raw_t)TICRATE,
		TIC_ScaleRawWith(year, TICRATE, freq));
	// Ten years, past which no server runs.
	const tictime_raw_t decade = year * 10ull;
	EXPECT_EQ(3600ull * 24ull * 365ull * 10000ull, TIC_ScaleRawWith(decade, 1000, freq));
}

// TicsToRaw rounds up, so a tic's raw start is never before the tic.
TEST(TicTime, ATicsRawStartIsNeverEarlyAndTicsAdvanceTogether)
{
	for (unsigned int i = 0; i < sizeof(FREQS) / sizeof(FREQS[0]); ++i)
	{
		const tictime_raw_t freq = FREQS[i];
		tictime_raw_t last = TIC_TicsToRawWith(0, freq);
		EXPECT_EQ(0ull, last);
		for (tictime_raw_t tic = 1; tic <= 200ull; ++tic)
		{
			const tictime_raw_t raw = TIC_TicsToRawWith(tic, freq);
			EXPECT_GT(raw, last) << "freq " << freq << " tic " << tic;
			EXPECT_GE(TIC_ScaleRawWith(raw, TICRATE, freq), tic);
			last = raw;
		}
	}
}

// The clock the platform actually has. These two cannot be given a fixed
// answer, so what is checked is what a caller depends on.
TEST(TicTime, ThePlatformsFrequencyClearsTicrate)
{
	TIC_Init();
	const tictime_raw_t freq = TIC_GetRawFrequency();
	EXPECT_GE(freq, (tictime_raw_t)TICRATE)
		<< "below TICRATE the round-trip guarantees above do not hold";
	EXPECT_GT(freq, 0ull);
}

TEST(TicTime, ThePlatformsCounterIsMonotonic)
{
	TIC_Init();
	tictime_raw_t last = TIC_GetRawCounter();
	for (int i = 0; i < 2000; ++i)
	{
		const tictime_raw_t now = TIC_GetRawCounter();
		ASSERT_GE(now, last) << "the counter went backwards at read " << i;
		last = now;
	}
}

// The convenience pair reads the platform's frequency and otherwise agrees
// with the cores, which is all it promises.
TEST(TicTime, TheConvenienceFormsAgreeWithTheCores)
{
	TIC_Init();
	const tictime_raw_t freq = TIC_GetRawFrequency();
	const tictime_raw_t raws[] = { 0ull, 1ull, freq, freq * 7ull + 3ull };
	for (unsigned int i = 0; i < sizeof(raws) / sizeof(raws[0]); ++i)
	{
		EXPECT_EQ(TIC_ScaleRawWith(raws[i], 1000, freq), TIC_ScaleRaw(raws[i], 1000));
		EXPECT_EQ(TIC_ScaleRawWith(raws[i], TICRATE, freq), TIC_ScaleRaw(raws[i], TICRATE));
	}
	for (tictime_raw_t tic = 0; tic <= 70ull; tic += 7ull)
		EXPECT_EQ(TIC_TicsToRawWith(tic, freq), TIC_TicsToRaw(tic));
}
