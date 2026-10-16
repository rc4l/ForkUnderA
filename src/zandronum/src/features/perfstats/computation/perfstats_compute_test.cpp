// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/perfstats/computation/perfstats_compute.h"

#include <gtest/gtest.h>

using zx::perfstats::ComputeFrameTimeSummary;
using zx::perfstats::PerfSummary;

TEST(PerfStats, BasicStats)
{
	PerfSummary s = ComputeFrameTimeSummary(std::vector<double>(10, 10.0));
	EXPECT_EQ(s.n, 10);
	EXPECT_DOUBLE_EQ(s.mean, 10.0);
	EXPECT_DOUBLE_EQ(s.min, 10.0);
	EXPECT_DOUBLE_EQ(s.max, 10.0);
	EXPECT_DOUBLE_EQ(s.fpsAvg, 100.0);
	EXPECT_DOUBLE_EQ(s.fps1pctLow, 100.0);
}

// [rc4l] One 100 ms spike among 99 smooth frames hides in the mean and even p99, and only the max and the 1% low show it.
TEST(PerfStats, CatchesTheStutterInTheTail)
{
	std::vector<double> f(99, 10.0);
	f.push_back(100.0);
	PerfSummary s = ComputeFrameTimeSummary(f);
	EXPECT_EQ(s.n, 100);
	EXPECT_NEAR(s.mean, 10.9, 0.001);
	EXPECT_GT(s.fpsAvg, 90.0);
	EXPECT_DOUBLE_EQ(s.p99, 10.0);
	EXPECT_DOUBLE_EQ(s.max, 100.0);
	EXPECT_DOUBLE_EQ(s.fps1pctLow, 10.0);
}

TEST(PerfStats, PercentilesTakeTheNearestRank)
{
	PerfSummary s = ComputeFrameTimeSummary({4.0, 1.0, 3.0, 2.0, 5.0});
	EXPECT_DOUBLE_EQ(s.min, 1.0);
	EXPECT_DOUBLE_EQ(s.p50, 3.0);
	EXPECT_DOUBLE_EQ(s.p95, 5.0);
}

TEST(PerfStats, EmptyIsZeroed)
{
	PerfSummary s = ComputeFrameTimeSummary({});
	EXPECT_EQ(s.n, 0);
	EXPECT_DOUBLE_EQ(s.fpsAvg, 0.0);
}

// [rc4l] Frames that took no time have no rate rather than an infinite one.
TEST(PerfStats, ZeroFrameTimesHaveNoRate)
{
	PerfSummary s = ComputeFrameTimeSummary(std::vector<double>(3, 0.0));
	EXPECT_EQ(s.n, 3);
	EXPECT_DOUBLE_EQ(s.fpsAvg, 0.0);
	EXPECT_DOUBLE_EQ(s.fps1pctLow, 0.0);
}
