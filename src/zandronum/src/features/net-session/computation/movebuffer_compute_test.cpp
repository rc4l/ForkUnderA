// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-session/computation/movebuffer_compute.h"

#include <gtest/gtest.h>

using namespace zx::net;

TEST(MoveBuffer, ASecondCommandRunsWhileAnotherWaitsBehindIt)
{
	EXPECT_TRUE(ComputeRunsSecondMoveCmd(2, 0, 35));
	EXPECT_TRUE(ComputeRunsSecondMoveCmd(5, 0, 35));
}

TEST(MoveBuffer, TheLastBufferedCommandWaitsUntilArrivalsAreSteady)
{
	EXPECT_FALSE(ComputeRunsSecondMoveCmd(1, 0, 35));
	EXPECT_FALSE(ComputeRunsSecondMoveCmd(1, 34, 35));
	EXPECT_TRUE(ComputeRunsSecondMoveCmd(1, 35, 35));
}

TEST(MoveBuffer, NoBufferedMoveLeavesTheOtherCommandsToRun)
{
	EXPECT_TRUE(ComputeRunsSecondMoveCmd(0, 0, 35));
}

TEST(MoveBuffer, NoCommandSinceJoiningSaysNothing)
{
	EXPECT_EQ(MOVE_ARRIVAL_UNKNOWN, ComputeMoveArrival(0, 100));
}

TEST(MoveBuffer, ACommandThisTicIsOnTime)
{
	EXPECT_EQ(MOVE_ARRIVAL_ON_TIME, ComputeMoveArrival(100, 100));
}

TEST(MoveBuffer, NoCommandThisTicIsMissed)
{
	EXPECT_EQ(MOVE_ARRIVAL_MISSED, ComputeMoveArrival(99, 100));
	EXPECT_EQ(MOVE_ARRIVAL_MISSED, ComputeMoveArrival(90, 100));
}
