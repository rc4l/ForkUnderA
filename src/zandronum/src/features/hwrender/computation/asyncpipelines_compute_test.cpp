// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/asyncpipelines_compute.h"

using namespace zx::hwrender;

TEST(AsyncPipelines, ADeviceThatCanWaitMakesThemInTheBackgroundWhenWanted)
{
	EXPECT_TRUE(ComputeAsyncPipelines(true, true));
	EXPECT_FALSE(ComputeAsyncPipelines(false, true));
}

// [rc4l] A player's saved config can hold the cvar on, so the device has to overrule it, not the cvar's default.
TEST(AsyncPipelines, ABrowsersDeviceNeverDoesSinceItCannotWaitForThem)
{
	EXPECT_FALSE(ComputeAsyncPipelines(true, false));
	EXPECT_FALSE(ComputeAsyncPipelines(false, false));
}
