// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gameloop/computation/threadedtic_compute.h"

#include <gtest/gtest.h>

using zx::gameloop::ComputeTakeBackDue;
using zx::gameloop::ComputeThreadedTic;
using zx::gameloop::ThreadedTicIn;

namespace {
const ThreadedTicIn kOn = { true, true, false, false, false, false, false, false, false, false, false };
}

TEST(ThreadedTic, ALevelTheBackendDrawsAloneRunsOnTheGameThread)
{
	EXPECT_TRUE(ComputeThreadedTic(kOn));
}

TEST(ThreadedTic, AnythingThatReadsTheLiveGameOrHoldsTicsBackRunsInline)
{
	ThreadedTicIn in = kOn; in.enabled = false; EXPECT_FALSE(ComputeThreadedTic(in));
	in = kOn; in.inLevel = false; EXPECT_FALSE(ComputeThreadedTic(in));
	in = kOn; in.glDraws = true; EXPECT_FALSE(ComputeThreadedTic(in));
	in = kOn; in.wipePending = true; EXPECT_FALSE(ComputeThreadedTic(in));
	in = kOn; in.automap = true; EXPECT_FALSE(ComputeThreadedTic(in));
	in = kOn; in.menuOrConsole = true; EXPECT_FALSE(ComputeThreadedTic(in));
	in = kOn; in.paused = true; EXPECT_FALSE(ComputeThreadedTic(in));
	in = kOn; in.advanceDemo = true; EXPECT_FALSE(ComputeThreadedTic(in));
	in = kOn; in.singleTics = true; EXPECT_FALSE(ComputeThreadedTic(in));
	in = kOn; in.copyCheck = true; EXPECT_FALSE(ComputeThreadedTic(in));
	in = kOn; in.resizePending = true; EXPECT_FALSE(ComputeThreadedTic(in));
}

TEST(ThreadedTic, ATicIsTakenBackOnTheBoundaryAfterTheOneItStartedIn)
{
	// [rc4l] Started at gametic 10, in clock tic 11.
	EXPECT_FALSE(ComputeTakeBackDue(true, 11, 10));
	EXPECT_TRUE(ComputeTakeBackDue(true, 12, 10));
	EXPECT_TRUE(ComputeTakeBackDue(true, 14, 10));
}

TEST(ThreadedTic, AnUnfinishedTicIsNeverTakenBack)
{
	EXPECT_FALSE(ComputeTakeBackDue(false, 20, 10));
}
