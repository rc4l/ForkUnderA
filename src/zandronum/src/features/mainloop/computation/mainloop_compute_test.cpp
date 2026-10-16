// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/mainloop/computation/mainloop_compute.h"

using namespace zx::mainloop;

// [rc4l] A wipe tic is due every 25 ms; nothing is due before the first.
TEST(MainLoop, AWipeTicIsDueEvery25Milliseconds)
{
	EXPECT_EQ(0, ComputeWipeTicsDue(1000, 1000));
	EXPECT_EQ(0, ComputeWipeTicsDue(1000, 1024));
	EXPECT_EQ(1, ComputeWipeTicsDue(1000, 1025));
	EXPECT_EQ(4, ComputeWipeTicsDue(1000, 1100));
}

// [rc4l] The millisecond clock wrapping between the two still counts the time between them.
TEST(MainLoop, AClockWrapStillCounts)
{
	EXPECT_EQ(2, ComputeWipeTicsDue(0xFFFFFFF0u, 40));
}

// [rc4l] The quit sound's 105 refreshes are a second and a half, and nothing is no wait.
TEST(MainLoop, AVblIsASeventiethOfASecond)
{
	EXPECT_EQ(1500u, ComputeVblMilliseconds(105));
	EXPECT_EQ(28u, ComputeVblMilliseconds(2));
	EXPECT_EQ(0u, ComputeVblMilliseconds(0));
	EXPECT_EQ(0u, ComputeVblMilliseconds(-3));
}

// [rc4l] A deadline is reached at its millisecond and after, including past a wrap of the clock.
TEST(MainLoop, ADeadlinePassesAtItsMillisecond)
{
	EXPECT_FALSE(ComputeDeadlinePassed(2000, 1999));
	EXPECT_TRUE(ComputeDeadlinePassed(2000, 2000));
	EXPECT_TRUE(ComputeDeadlinePassed(2000, 2500));
	EXPECT_FALSE(ComputeDeadlinePassed(10, 0xFFFFFFF0u));
	EXPECT_TRUE(ComputeDeadlinePassed(0xFFFFFFF0u, 10));
}
