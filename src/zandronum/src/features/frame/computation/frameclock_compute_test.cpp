// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/frameclock_compute.h"

#include <gtest/gtest.h>

using namespace zx::frame;

// [rc4l] A pinned clock holds every capture's animations at the same frame however long the capture took.
TEST(FrameClock, APinnedClockReplacesTheRealOne)
{
	EXPECT_EQ(1000u, ComputeFrameClock(1000, 123456u));
}

TEST(FrameClock, NoPinReadsTheRealClock)
{
	EXPECT_EQ(123456u, ComputeFrameClock(0, 123456u));
	EXPECT_EQ(123456u, ComputeFrameClock(-3, 123456u));
}
