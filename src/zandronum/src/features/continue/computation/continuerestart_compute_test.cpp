// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/continue/computation/continuerestart_compute.h"

#include <gtest/gtest.h>

using zx::ComputeAfterRestartDue;

// [Doruk-Aksoy] The frame after wad_reload asks for its restart, which has not happened yet: a load here went onto the old WAD set and crashed.
TEST(ComputeAfterRestartDue, WaitsForTheRestartItWasArmedBefore)
{
	EXPECT_FALSE(ComputeAfterRestartDue(true, 0, 0, false));
	EXPECT_FALSE(ComputeAfterRestartDue(true, 3, 3, false));
}

TEST(ComputeAfterRestartDue, WaitsOutTheStartupAfterTheRestart)
{
	EXPECT_FALSE(ComputeAfterRestartDue(true, 0, 1, true));
}

TEST(ComputeAfterRestartDue, RunsOnceTheRestartHasHappenedAndStartupIsOver)
{
	EXPECT_TRUE(ComputeAfterRestartDue(true, 0, 1, false));
	EXPECT_TRUE(ComputeAfterRestartDue(true, 2, 4, false));
}

TEST(ComputeAfterRestartDue, NothingArmedNeverRuns)
{
	EXPECT_FALSE(ComputeAfterRestartDue(false, 0, 1, false));
}
