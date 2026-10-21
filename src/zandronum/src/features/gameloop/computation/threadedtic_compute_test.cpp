// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gameloop/computation/threadedtic_compute.h"

#include <gtest/gtest.h>

using zx::gameloop::ComputeHeldFrame;
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

// [rc4l] Started in clock tic 11 and finished at once, it waits for clock tic 12: taken back inside 11, its result showed with the blend already at the slot's end and the view lurched a tic, then stepped back (queue #51).
TEST(ThreadedTic, ATicIsTakenBackOnTheBoundaryAfterTheOneItStartedIn)
{
	EXPECT_FALSE(ComputeTakeBackDue(true, 11, 11));
	EXPECT_TRUE(ComputeTakeBackDue(true, 12, 11));
	EXPECT_TRUE(ComputeTakeBackDue(true, 14, 11));
}

TEST(ThreadedTic, AnUnfinishedTicIsNeverTakenBack)
{
	EXPECT_FALSE(ComputeTakeBackDue(false, 20, 11));
}

// [rc4l] A frame holds only once its tic is due back and still running.
TEST(ThreadedTic, AFrameHoldsOnlyPastTheBoundaryWithTheTicStillRunning)
{
	EXPECT_FALSE(ComputeHeldFrame(false, 11, 11));
	EXPECT_TRUE(ComputeHeldFrame(false, 12, 11));
	EXPECT_FALSE(ComputeHeldFrame(true, 12, 11));
}

// [rc4l] nuts.wad with Complex Doom caught up 34 tics a frame, and each scanned and filled the render copy that only the last one's frame draws.
TEST(ThreadedTic, OnlyTheLastTicOfABatchIsDrawn)
{
	EXPECT_TRUE(zx::gameloop::ComputeTicDrawn(0));
	EXPECT_FALSE(zx::gameloop::ComputeTicDrawn(1));
	EXPECT_FALSE(zx::gameloop::ComputeTicDrawn(33));
	EXPECT_TRUE(zx::gameloop::ComputeTicDrawn(-1));   // [rc4l] a lone tic outside any batch
}
