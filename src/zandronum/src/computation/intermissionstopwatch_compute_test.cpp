// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "computation/intermissionstopwatch_compute.h"

TEST(IntermissionStopWatch, AClientCountsDownATicAtATime)
{
	EXPECT_EQ(zx::ComputeClientStopWatch(350, false), 349);
	EXPECT_EQ(zx::ComputeClientStopWatch(1, false), 0);
}

TEST(IntermissionStopWatch, APausedDemoHoldsTheCount)
{
	EXPECT_EQ(zx::ComputeClientStopWatch(350, true), 350);
}

TEST(IntermissionStopWatch, TheCountStopsAtZero)
{
	EXPECT_EQ(zx::ComputeClientStopWatch(0, false), 0);
	EXPECT_EQ(zx::ComputeClientStopWatch(-1, false), -1);
}
