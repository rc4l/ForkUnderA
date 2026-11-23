// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: clock_compute_test.cpp
//
// Description: Tests for clock_compute: the model's date for a tic, which
// source dates a sample's ages, convergence through noise and recovery from a
// step, and each way a sample re-seeds or is refused.
//
//-----------------------------------------------------------------------------

#include "clock_compute.h"
#include <gtest/gtest.h>

#include <math.h>

using namespace zx::netsnapshot;

// [Kaori-Chani] Feeds `n` samples on the line baseMS + msPerTic * (tic - firstTic) and returns
// how many were not accepted.
static unsigned int clocktest_Feed(SnapClockModel& m, int firstTic, int n, double baseMS,
	double msPerTic)
{
	unsigned int notAccepted = 0;
	for (int i = 0; i < n; ++i)
	{
		const SnapClockSampled s = ComputeClockSample(m, firstTic + i,
			(unsigned int)(baseMS + i * msPerTic));
		if (s.outcome != SNAP_CLOCK_ACCEPTED)
			++notAccepted;
	}
	return notAccepted;
}

// [Kaori-Chani] The date for a tic, on the line 1000 + 5 + 28.5 per tic from tic 100.
TEST(SnapshotClock, TheModelDatesATicOnItsLine)
{
	static const struct { int tic; double want; } cases[] =
	{
		{ 100, 1005.0 },	// the base tic
		{ 110, 1290.0 },	// after it
		{ 90, 720.0 },		// before it
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_NEAR(cases[i].want, ComputeClockDate(1000, 5.0, 28.5, 100, cases[i].tic), 1e-9)
			<< "tic " << cases[i].tic;
	}
}

TEST(SnapshotClock, TheAgeSourceFollowsTrustAndJitter)
{
	static const struct { bool bTrusted; double jitter; int max; int want; } cases[] =
	{
		{ false, 3.0, 50, SNAP_AGE_UNTRUSTED },	// untrusted, whatever the jitter
		{ true, 3.0, 50, SNAP_AGE_MODEL },
		{ true, 50.0, 50, SNAP_AGE_MODEL },		// on the limit
		{ true, 50.5, 50, SNAP_AGE_JITTERY },
		{ true, 276.7, 0, SNAP_AGE_MODEL },		// 0: never falls back
		{ true, 276.7, -5, SNAP_AGE_MODEL },	// nor below it
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_EQ(cases[i].want, ComputeAgeSource(cases[i].bTrusted, cases[i].jitter, cases[i].max))
			<< "case " << i << ": jitter " << cases[i].jitter << ", max " << cases[i].max;
	}
}

TEST(SnapshotClock, AFreshModelIsNeitherReadyNorTrusted)
{
	SnapClockModel m;
	EXPECT_FALSE(ComputeClockReady(m));
	EXPECT_FALSE(ComputeClockTrusted(m));
	EXPECT_EQ(1000.0 / TICRATE, ComputeClockMsPerTic(m));
	EXPECT_TRUE(ComputeClockFeeds(m, -5));
}

// [Kaori-Chani] 30.0 ms/tic with deterministic jitter, then the clock jumps 10 s and the model
// must recover to the new regime.
TEST(SnapshotClock, TheModelConvergesThroughNoiseAndRecoversFromAStep)
{
	SnapClockModel m;
	for (int i = 0; i < 200; ++i)
		ComputeClockSample(m, 1000 + i, (unsigned int)(50000.0 + i * 30.0 + ((i * 7) % 7) - 3));
	EXPECT_TRUE(ComputeClockReady(m));
	EXPECT_NEAR(30.0, ComputeClockMsPerTic(m), 0.5);

	for (int i = 200; i < 500; ++i)
		ComputeClockSample(m, 1000 + i, (unsigned int)(60000.0 + i * 30.0));
	EXPECT_NEAR(30.0, ComputeClockMsPerTic(m), 0.5) << "after a ten-second step";
}

// [Kaori-Chani] Only a newer tic feeds, and a trusted fit maps a time back to its tic.
TEST(SnapshotClock, OnlyANewerTicFeedsAndATrustedFitMapsATimeBack)
{
	SnapClockModel m;
	for (int i = 0; i < 200; ++i)
		ComputeClockSample(m, 1000 + i, (unsigned int)(50000.0 + i * 30.0 + ((i * 7) % 7) - 3));
	for (int i = 200; i < 500; ++i)
		ComputeClockSample(m, 1000 + i, (unsigned int)(60000.0 + i * 30.0));

	EXPECT_FALSE(ComputeClockFeeds(m, 1499));
	EXPECT_TRUE(ComputeClockFeeds(m, 1500));

	double tic = 0.0;
	ASSERT_TRUE(ComputeClockTrusted(m));
	ASSERT_TRUE(ComputeClockTicAt(m, 60000 + 1400 * 30, tic));
	EXPECT_NEAR(2400.0, tic, 0.5);
}

TEST(SnapshotClock, AnUntrustedModelMapsNoTime)
{
	SnapClockModel fresh;
	double tic = 0.0;
	EXPECT_FALSE(ComputeClockTicAt(fresh, 1000, tic));
}

// [Kaori-Chani] Ready from CLENT_CLOCK_MIN_READY samples, trusted from CLENT_CLOCK_TRUST_SAMPLES.
TEST(SnapshotClock, ReadyAndTrustedArriveAtTheirSampleCounts)
{
	const int ready = CLENT_CLOCK_MIN_READY, trust = CLENT_CLOCK_TRUST_SAMPLES;
	SnapClockModel n;

	clocktest_Feed(n, 0, ready - 1, 0.0, 30.0);
	EXPECT_FALSE(ComputeClockReady(n)) << "one sample short of ready";
	clocktest_Feed(n, ready - 1, 1, 30.0 * (ready - 1), 30.0);
	EXPECT_TRUE(ComputeClockReady(n));

	clocktest_Feed(n, ready, trust - ready - 1, 30.0 * ready, 30.0);
	EXPECT_FALSE(ComputeClockTrusted(n)) << "one sample short of trusted";
	clocktest_Feed(n, trust - 1, 1, 30.0 * (trust - 1), 30.0);
	EXPECT_TRUE(ComputeClockTrusted(n));
}

// [Kaori-Chani] Under two samples, or every sample on one tic, there is no line.
TEST(SnapshotClock, NoLineUnderTwoSamplesOrOnASingleTic)
{
	SnapClockModel m;
	ComputeClockRefit(m);
	EXPECT_FALSE(m.fitValid);

	EXPECT_EQ(SNAP_CLOCK_ACCEPTED, ComputeClockSample(m, 10, 1000).outcome);
	EXPECT_EQ(SNAP_CLOCK_ACCEPTED, ComputeClockSample(m, 10, 1030).outcome);
	EXPECT_EQ(2u, m.count);
	EXPECT_FALSE(m.fitValid);
}

// [Kaori-Chani] A tic going back, arrival going back, or a gap past the step re-seeds on that
// sample; a gap of exactly the step does not.
TEST(SnapshotClock, AnOutOfOrderOrSteppedSampleReseeds)
{
	SnapClockModel m;
	ComputeClockSample(m, 10, 1000);
	ComputeClockSample(m, 10, 1030);

	EXPECT_EQ(SNAP_CLOCK_RESET, ComputeClockSample(m, 9, 1100).outcome) << "a tic going back";
	EXPECT_EQ(0u, m.count);
	EXPECT_EQ(9, m.baseTic);
	EXPECT_EQ(1100u, m.baseMS);
	EXPECT_TRUE(m.haveLast);

	EXPECT_EQ(SNAP_CLOCK_RESET, ComputeClockSample(m, 20, 1000).outcome) << "arrival going back";
	EXPECT_EQ(20, m.baseTic);
	EXPECT_EQ(1000u, m.baseMS);

	EXPECT_EQ(SNAP_CLOCK_ACCEPTED,
		ComputeClockSample(m, 21, 1000 + CLENT_CLOCK_STEP_MS).outcome) << "exactly the step";
	EXPECT_EQ(SNAP_CLOCK_RESET,
		ComputeClockSample(m, 22, 1001 + 2 * CLENT_CLOCK_STEP_MS).outcome) << "past the step";
}

// [Kaori-Chani] On a steady line the floor decides: 2.5 s off is rejected, and a run of
// rejections re-seeds on the last.
TEST(SnapshotClock, ARunOfFarSamplesReseedsOnTheLast)
{
	SnapClockModel r;
	ASSERT_EQ(0u, clocktest_Feed(r, 100, 20, 10000.0, 30.0));
	ASSERT_TRUE(r.fitValid);

	unsigned int rejected = 0;
	int reseededAt = -1;
	for (int i = 0; (i < CLENT_CLOCK_MAX_SKIP_RUN) && (reseededAt < 0); ++i)
	{
		const SnapClockSampled s = ComputeClockSample(r, 120 + i,
			(unsigned int)(10000.0 + (20 + i) * 30.0 + 2500.0));
		if (s.outcome == SNAP_CLOCK_REJECTED)
			++rejected;
		else if (s.outcome == SNAP_CLOCK_RESEEDED)
			reseededAt = i;
	}
	EXPECT_EQ((unsigned int)(CLENT_CLOCK_MAX_SKIP_RUN - 1), rejected);
	EXPECT_EQ(CLENT_CLOCK_MAX_SKIP_RUN - 1, reseededAt);
	EXPECT_EQ(0u, r.count);
	EXPECT_EQ(0u, r.accepted);
	EXPECT_EQ(120 + CLENT_CLOCK_MAX_SKIP_RUN - 1, r.baseTic);
	EXPECT_TRUE(r.haveBase);
}

// [Kaori-Chani] A small residual is accepted and reported, and clears the skip run.
TEST(SnapshotClock, ANearSampleIsAcceptedWithItsResidual)
{
	SnapClockModel q;
	clocktest_Feed(q, 100, 20, 10000.0, 30.0);

	const SnapClockSampled sampled = ComputeClockSample(q, 120, 10000 + 20 * 30 + 40);
	EXPECT_EQ(SNAP_CLOCK_ACCEPTED, sampled.outcome);
	EXPECT_TRUE(sampled.bResidual);
	EXPECT_NEAR(40.0, sampled.residual, 5.0);
	EXPECT_EQ(0u, q.skipRun);
	EXPECT_EQ(21u, q.count);
}

// [Kaori-Chani] Past the floor's reach the spread decides: a stream jittering by 600 ms accepts
// 2.5 s off, which the floor alone would reject.
TEST(SnapshotClock, AWideSpreadAcceptsWhatTheFloorAloneWouldReject)
{
	SnapClockModel j;
	for (int i = 0; i < 60; ++i)
		ComputeClockSample(j, 1000 + 50 * i, (unsigned int)(100000.0 + i * 1500.0
			+ ((i & 1) ? 600.0 : -600.0)));
	ASSERT_GT(8.0 * j.meanAbsResid, CLENT_CLOCK_OUTLIER_FLOOR_MS) << "the spread must pass the floor";

	const SnapClockSampled sampled = ComputeClockSample(j, 1000 + 50 * 60,
		(unsigned int)(100000.0 + 60 * 1500.0 + 2500.0));
	EXPECT_EQ(SNAP_CLOCK_ACCEPTED, sampled.outcome);
	EXPECT_TRUE(sampled.bResidual);
	EXPECT_GT(sampled.residual, CLENT_CLOCK_OUTLIER_FLOOR_MS);
}
