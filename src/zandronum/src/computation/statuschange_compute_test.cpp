// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "computation/statuschange_compute.h"

TEST(StatusChange, AMoveOnTheTicTheStatusChangedLeavesIt)
{
	EXPECT_FALSE(zx::ComputeMoveClearsStatuses(500, 500));
}

TEST(StatusChange, AMoveOnALaterTicClearsIt)
{
	EXPECT_TRUE(zx::ComputeMoveClearsStatuses(500, 501));
	EXPECT_TRUE(zx::ComputeMoveClearsStatuses(0, 501));
}
