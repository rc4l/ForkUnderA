//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: replay_compute_test.cpp
//
// Description: Tests for replay_compute: the distance in whole units, the
// error histogram's bins and p99, and the smoothing census added, taken back
// and split.
//
//-----------------------------------------------------------------------------

#include "replay_compute.h"
#include <gtest/gtest.h>

#include <string.h>

using namespace zx::netsnapshot;

// [Kaori-Chani] A reading is compared to well under a bin, since the engine may compute in x87
// extended precision, where an exact compare can fail.
static const double REPLAYTEST_TOL = 1.0e-6;

// [Kaori-Chani] A census with every field set apart, from `base`.
static SnapSmoothStats replaytest_Census(unsigned int base)
{
	SnapSmoothStats s;
	memset(&s, 0, sizeof(s));
	for (unsigned int p = 0; p < 2; ++p)
	{
		s.folds[p] = base + 1 + p;
		s.snaps[p] = base + 3 + p;
		s.foldSum[p] = base + 5.5 + p;
		for (unsigned int b = 0; b < 4; ++b)
			s.resid[p][b] = base + 10 + p * 4 + b;
	}
	s.offFrames = base + 20;
	s.past = base + 21;
	s.zero = base + 22;
	s.reversed = base + 23;
	s.movingFrames = base + 24;
	for (unsigned int k = 0; k < SNAP_SMOOTH_KINDS; ++k)
	{
		for (unsigned int a = 0; a < 2; ++a)
		{
			SnapSmoothSplit& c = s.ex[k][a];
			const unsigned int at = base + 30 + (k * 2 + a) * 11;
			c.folds = at; c.snaps = at + 1; c.snapsBack = at + 2;
			c.reversed = at + 3; c.moving = at + 4;
			c.snapSize[0] = at + 5; c.snapSize[1] = at + 6; c.snapSize[2] = at + 7;
			c.off15 = at + 8; c.off45 = at + 9; c.foldSum = at + 10.5;
		}
	}
	return s;
}

// [Kaori-Chani] Whether two censuses agree on every field, naming the first that does not.
static ::testing::AssertionResult replaytest_Same(const SnapSmoothStats& a, const SnapSmoothStats& b)
{
#define REPLAYTEST_EQ(field) \
	if (!((a.field) == (b.field))) \
		return ::testing::AssertionFailure() << #field << " differs"
	REPLAYTEST_EQ(offFrames);
	REPLAYTEST_EQ(past);
	REPLAYTEST_EQ(zero);
	REPLAYTEST_EQ(reversed);
	REPLAYTEST_EQ(movingFrames);
	for (unsigned int p = 0; p < 2; ++p)
	{
		REPLAYTEST_EQ(folds[p]);
		REPLAYTEST_EQ(snaps[p]);
		REPLAYTEST_EQ(foldSum[p]);
		for (unsigned int k = 0; k < 4; ++k)
			REPLAYTEST_EQ(resid[p][k]);
	}
	for (unsigned int k = 0; k < SNAP_SMOOTH_KINDS; ++k)
	{
		for (unsigned int d = 0; d < 2; ++d)
		{
			REPLAYTEST_EQ(ex[k][d].folds);
			REPLAYTEST_EQ(ex[k][d].snaps);
			REPLAYTEST_EQ(ex[k][d].snapsBack);
			REPLAYTEST_EQ(ex[k][d].reversed);
			REPLAYTEST_EQ(ex[k][d].moving);
			REPLAYTEST_EQ(ex[k][d].foldSum);
			REPLAYTEST_EQ(ex[k][d].snapSize[0]);
			REPLAYTEST_EQ(ex[k][d].snapSize[1]);
			REPLAYTEST_EQ(ex[k][d].snapSize[2]);
			REPLAYTEST_EQ(ex[k][d].off15);
			REPLAYTEST_EQ(ex[k][d].off45);
		}
	}
#undef REPLAYTEST_EQ
	return ::testing::AssertionSuccess();
}

// [Kaori-Chani] The distance, truncated to whole units, in any direction.
TEST(SnapshotReplay, DistanceTruncatesToWholeUnits)
{
	const fixed_t u = FRACUNIT;
	EXPECT_EQ(0, ComputeDistUnits(0, 0, 0, 0, 0, 0));
	EXPECT_EQ(5, ComputeDistUnits(0, 0, 0, 3 * u, 4 * u, 0));
	EXPECT_EQ(13, ComputeDistUnits(3 * u, 4 * u, 12 * u, 0, 0, 0));
	EXPECT_EQ(3, ComputeDistUnits(-u, -u, -u, u, u, u));			// 3.46
	EXPECT_EQ(0, ComputeDistUnits(0, 0, 0, u - 1, 0, 0));
	EXPECT_EQ(30000, ComputeDistUnits(0, 0, 0, 0, 0, -30000 * u));
}

// [Kaori-Chani] The histogram, with errors mid-bin, as one on an edge falls either side.
TEST(SnapshotReplay, HistogramCountsEachErrorInItsBin)
{
	static unsigned int hist[SNAP_REPLAY_BINS];
	memset(hist, 0, sizeof(hist));
	ComputeBinAdd(hist, 0.0);
	ComputeBinAdd(hist, 0.05);
	ComputeBinAdd(hist, 0.15);
	ComputeBinAdd(hist, 409.45);
	ComputeBinAdd(hist, 409.55);
	ComputeBinAdd(hist, 1.0e9);
	EXPECT_EQ(2u, hist[0]);
	EXPECT_EQ(1u, hist[1]);
	EXPECT_EQ(1u, hist[4094]);
	EXPECT_EQ(2u, hist[SNAP_REPLAY_BINS - 1]);
}

// [Kaori-Chani] 991 under 0.1 and 9 at 5.0 put the sample at 990 under 0.1; one more at 5.0 moves
// it.
TEST(SnapshotReplay, HistogramP99MovesWithTheSample)
{
	static unsigned int hist[SNAP_REPLAY_BINS];
	memset(hist, 0, sizeof(hist));
	hist[0] = 991;
	hist[50] = 9;
	EXPECT_NEAR(0.1, ComputeBinP99(hist, 1000), REPLAYTEST_TOL);
	hist[0] = 990;
	hist[50] = 10;
	EXPECT_NEAR(5.1, ComputeBinP99(hist, 1000), REPLAYTEST_TOL);
}

// [Kaori-Chani] Nothing counted reads the last bin's edge; everything past reads it too.
TEST(SnapshotReplay, HistogramP99ReadsTheLastEdgeWhenEmptyOrPastIt)
{
	static unsigned int hist[SNAP_REPLAY_BINS];
	memset(hist, 0, sizeof(hist));
	EXPECT_NEAR(SNAP_REPLAY_BINS / 10.0, ComputeBinP99(hist, 0), REPLAYTEST_TOL);
	hist[SNAP_REPLAY_BINS - 1] = 7;
	EXPECT_NEAR(SNAP_REPLAY_BINS / 10.0, ComputeBinP99(hist, 7), REPLAYTEST_TOL);
}

// [Kaori-Chani] A sum taken back is what it was.
TEST(SnapshotReplay, CensusAddsAndTakesBack)
{
	const SnapSmoothStats a = replaytest_Census(100), b = replaytest_Census(7);
	SnapSmoothStats d = a;

	ComputeCensusAdd(d, b, false);
	EXPECT_EQ(111u, d.folds[1]);
	EXPECT_EQ(141u, d.resid[1][3]);
	EXPECT_EQ(118.0, d.foldSum[0]);
	EXPECT_EQ(155u, d.movingFrames);
	EXPECT_EQ(151u, d.zero);
	EXPECT_EQ(167u, d.ex[0][0].folds);
	EXPECT_EQ(241u, d.ex[1][1].moving);
	EXPECT_EQ(210.0, d.ex[0][1].foldSum);
	EXPECT_EQ(295u, d.ex[2][1].off45);
	EXPECT_EQ(269u, d.ex[2][0].snapSize[2]);

	ComputeCensusAdd(d, b, true);
	EXPECT_TRUE(replaytest_Same(d, a));
}

// [Kaori-Chani] The fold half keeps what was folded and the drawn half what was drawn.
TEST(SnapshotReplay, CensusSplitsFoldFromDrawn)
{
	const SnapSmoothStats a = replaytest_Census(100);
	SnapSmoothStats fold, drawn;

	ComputeCensusSplit(a, fold, drawn);

	EXPECT_EQ(0u, fold.offFrames);
	EXPECT_EQ(0u, fold.past);
	EXPECT_EQ(0u, fold.reversed);
	EXPECT_EQ(0u, fold.movingFrames);
	EXPECT_EQ(a.zero, fold.zero);
	EXPECT_EQ(a.folds[0], fold.folds[0]);
	EXPECT_EQ(0u, drawn.folds[0]);
	EXPECT_EQ(0u, drawn.zero);
	EXPECT_EQ(0.0, drawn.foldSum[1]);
	EXPECT_EQ(a.offFrames, drawn.offFrames);
	EXPECT_EQ(a.movingFrames, drawn.movingFrames);
	EXPECT_EQ(0u, fold.ex[1][0].moving);
	EXPECT_EQ(0u, fold.ex[1][0].reversed);
	EXPECT_EQ(a.ex[1][0].snapsBack, fold.ex[1][0].snapsBack);
	EXPECT_EQ(a.ex[1][0].moving, drawn.ex[1][0].moving);
	EXPECT_EQ(0u, drawn.ex[1][0].snaps);
	EXPECT_EQ(0.0, drawn.ex[0][1].foldSum);
	EXPECT_EQ(0u, fold.ex[2][0].off15);
	EXPECT_EQ(a.ex[2][0].off45, drawn.ex[2][0].off45);
	EXPECT_EQ(a.ex[2][1].snapSize[1], fold.ex[2][1].snapSize[1]);
	EXPECT_EQ(0u, drawn.ex[2][1].snapSize[1]);
}

// [Kaori-Chani] A split's two halves add back to the whole.
TEST(SnapshotReplay, CensusSplitHalvesAddBackToTheWhole)
{
	const SnapSmoothStats a = replaytest_Census(100);
	SnapSmoothStats fold, drawn, sum;

	ComputeCensusSplit(a, fold, drawn);
	memset(&sum, 0, sizeof(sum));
	ComputeCensusAdd(sum, fold, false);
	ComputeCensusAdd(sum, drawn, false);

	EXPECT_TRUE(replaytest_Same(sum, a));
}
