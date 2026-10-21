// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/frame-limit/computation/framelimit_compute.h"

using namespace zx::framelimit;

TEST(ComputeFrameLimitFps, CapfpsHoldsTheTicRateOverMaxfps)
{
	EXPECT_EQ(35, ComputeFrameLimitFps(500, true, 35));
	EXPECT_EQ(35, ComputeFrameLimitFps(0, true, 35));
	EXPECT_EQ(144, ComputeFrameLimitFps(144, false, 35));
	EXPECT_EQ(0, ComputeFrameLimitFps(0, false, 35));
	EXPECT_EQ(0, ComputeFrameLimitFps(-5, false, 35));
}

TEST(ComputeFrameDeadline, APeriodAfterThePreviousFrame)
{
	EXPECT_EQ(1016666, ComputeFrameDeadline(1000000, 1001000, 60));
	// [rc4l] A frame shown a little late keeps the schedule, so the rate holds rather than drifting under it.
	EXPECT_EQ(1016666, ComputeFrameDeadline(1000000, 1020000, 60));
}

TEST(ComputeFrameDeadline, NowWithNoLimitNoPreviousFrameOrALostSchedule)
{
	EXPECT_EQ(5000, ComputeFrameDeadline(1000000, 5000, 0));
	EXPECT_EQ(5000, ComputeFrameDeadline(0, 5000, 60));
	// [rc4l] More than a period behind starts again from now, so a stall is not followed by a burst of frames.
	EXPECT_EQ(1040000, ComputeFrameDeadline(1000000, 1040000, 60));
	// [rc4l] A clock that went back more than a second is not waited out.
	EXPECT_EQ(10, ComputeFrameDeadline(5000000, 10, 60));
}

TEST(ComputeFrameLimitSleepUs, SleepsToTwoMillisecondsBeforeThenYields)
{
	EXPECT_EQ(8000, ComputeFrameLimitSleepUs(10000, 0));
	EXPECT_EQ(0, ComputeFrameLimitSleepUs(2000, 0));
	EXPECT_EQ(0, ComputeFrameLimitSleepUs(1, 0));
	EXPECT_EQ(-1, ComputeFrameLimitSleepUs(100, 100));
	EXPECT_EQ(-1, ComputeFrameLimitSleepUs(100, 200));
	EXPECT_EQ(-1, ComputeFrameLimitSleepUs(1000001, 0));
}

TEST(ComputeSteppedFrameDue, ABrowserFrameWithinToleranceOfTheDeadlineIsDrawn)
{
	EXPECT_TRUE(ComputeSteppedFrameDue(16666, 16666, 2000));
	EXPECT_TRUE(ComputeSteppedFrameDue(16666, 14666, 2000));
	EXPECT_FALSE(ComputeSteppedFrameDue(16666, 14665, 2000));
}

// [rc4l] A 60 frame limit on a 144 Hz browser draws 60 of the 144 offered frames a second, not the 48 a period counted from each drawn frame would.
TEST(ComputeSteppedFrame, ALimitBelowTheRefreshKeepsItsRate)
{
	const long long refresh = 1000000 / 144;
	FrameLimiter l;
	int drawn = 0;
	for (int i = 1; i <= 144; i++)
		if (ComputeSteppedFrame(l, i * refresh, 60, 2000)) drawn++;
	EXPECT_GE(drawn, 59);
	EXPECT_LE(drawn, 61);
}

TEST(ComputeSteppedFrame, EveryFrameWithNoLimit)
{
	FrameLimiter l;
	l.deadlineUs = 5000;
	EXPECT_TRUE(ComputeSteppedFrame(l, 1000, 0, 2000));
	EXPECT_EQ(0, l.deadlineUs);
}

TEST(ComputePresentDeadline, ADesktopPresentWaitsAPeriodAfterTheLast)
{
	FrameLimiter l;
	EXPECT_EQ(1000000, ComputePresentDeadline(l, 1000000, 60, false));
	EXPECT_EQ(1016666, ComputePresentDeadline(l, 1001000, 60, false));
	EXPECT_EQ(1016666, l.deadlineUs);
	// [rc4l] With no limit there is nothing to wait for, and the schedule starts over.
	EXPECT_EQ(0, ComputePresentDeadline(l, 1002000, 0, false));
	EXPECT_EQ(0, l.deadlineUs);
}

// [rc4l] A browser presents every frame it draws, and a present that reset the deadline made every frame due, so vid_maxfps limited nothing there.
TEST(ComputePresentDeadline, ABrowsersPresentLeavesItsFramesDeadline)
{
	FrameLimiter l;
	const long long refresh = 1000000 / 480;
	int drawn = 0;
	for (int i = 1; i <= 480; i++)
	{
		if (!ComputeSteppedFrame(l, i * refresh, 100, 2000)) continue;
		drawn++;
		EXPECT_EQ(0, ComputePresentDeadline(l, i * refresh, 100, true));
	}
	EXPECT_GE(drawn, 99);
	EXPECT_LE(drawn, 101);
}
