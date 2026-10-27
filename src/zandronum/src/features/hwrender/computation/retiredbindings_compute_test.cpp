// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/hwrender/computation/retiredbindings_compute.h"

using namespace zx::hwrender;

// [Doruk-Aksoy] Retired bindings come back only once the GPU has finished their last frame, oldest first.
TEST(RetiredBindings, ComeBackOnceTheirFrameIsDone)
{
	RetiredBindings<int> retired;
	int dropped = 0, got = 0;
	EXPECT_FALSE(ComputeRetireBindings(retired, 10, 1, 4, dropped));
	EXPECT_FALSE(ComputeRetireBindings(retired, 12, 2, 4, dropped));
	EXPECT_FALSE(ComputeTakeRetiredBindings(retired, 9, got));
	EXPECT_TRUE(ComputeTakeRetiredBindings(retired, 11, got));
	EXPECT_EQ(1, got);
	EXPECT_FALSE(ComputeTakeRetiredBindings(retired, 11, got));
	EXPECT_TRUE(ComputeTakeRetiredBindings(retired, 12, got));
	EXPECT_EQ(2, got);
	EXPECT_FALSE(ComputeTakeRetiredBindings(retired, 100, got));
}

// [Doruk-Aksoy] Past what is kept, the oldest is handed back to be let go, so a GPU that falls behind cannot pile them up.
TEST(RetiredBindings, PastTheKeptOnesTheOldestIsLetGo)
{
	RetiredBindings<int> retired;
	int dropped = 0;
	EXPECT_FALSE(ComputeRetireBindings(retired, 1, 1, 2, dropped));
	EXPECT_FALSE(ComputeRetireBindings(retired, 2, 2, 2, dropped));
	EXPECT_TRUE(ComputeRetireBindings(retired, 3, 3, 2, dropped));
	EXPECT_EQ(1, dropped);
	EXPECT_EQ(2u, retired.waiting.size());
}
