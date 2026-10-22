// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/latency/computation/latency_compute.h"

#include <gtest/gtest.h>

using namespace zx::latency;

TEST(LatencyStamp, TheFirstUnshownMomentIsTakenOnceArmed)
{
	LatencyStamp s;
	long long since = -1;
	EXPECT_FALSE(ComputeTakeShown(s, since));
	ComputeStampAt(s, 100);
	ComputeStampAt(s, 150);
	EXPECT_FALSE(ComputeTakeShown(s, since));
	ComputeArm(s);
	ASSERT_TRUE(ComputeTakeShown(s, since));
	EXPECT_EQ(100, since);
	EXPECT_FALSE(ComputeTakeShown(s, since));
}

TEST(LatencyStamp, ArmingWithNothingStampedDoesNothing)
{
	LatencyStamp s;
	ComputeArm(s);
	long long since = -1;
	EXPECT_FALSE(ComputeTakeShown(s, since));
	ComputeStampAt(s, 40);
	ComputeArm(s);
	ASSERT_TRUE(ComputeTakeShown(s, since));
	EXPECT_EQ(40, since);
}
