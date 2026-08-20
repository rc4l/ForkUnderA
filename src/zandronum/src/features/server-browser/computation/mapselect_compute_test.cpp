// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/mapselect_compute.h"

using zx::ComputeDeselectAllChanges;
using zx::ComputeSelectAllChanges;

TEST(MapSelect, SelectAllChangesAListThatIsMissingSomething)
{
	EXPECT_TRUE(ComputeSelectAllChanges(0, 32));
	EXPECT_TRUE(ComputeSelectAllChanges(31, 32));
}

// The whole point: no confirmation for a press that would do nothing.
TEST(MapSelect, SelectAllChangesNothingWhenEveryMapIsAlreadyIn)
{
	EXPECT_FALSE(ComputeSelectAllChanges(32, 32));
	EXPECT_FALSE(ComputeSelectAllChanges(1, 1));
}

TEST(MapSelect, DeselectAllChangesAListThatHasSomethingIn)
{
	EXPECT_TRUE(ComputeDeselectAllChanges(32, 32));
	EXPECT_TRUE(ComputeDeselectAllChanges(1, 32));
}

TEST(MapSelect, DeselectAllChangesNothingWhenEveryMapIsAlreadyOut)
{
	EXPECT_FALSE(ComputeDeselectAllChanges(0, 32));
}

// An empty rotation has nothing to put in or take out, so neither button asks.
TEST(MapSelect, NeitherActionChangesAnEmptyList)
{
	EXPECT_FALSE(ComputeSelectAllChanges(0, 0));
	EXPECT_FALSE(ComputeDeselectAllChanges(0, 0));
}
