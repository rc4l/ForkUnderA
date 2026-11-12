// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/maprotation_compute.h"

#include <gtest/gtest.h>

using namespace zx::maprotation;

TEST(SetsMapUsedStatus, AServerMarksAnEntryInTheRotationEitherWay)
{
	EXPECT_TRUE(ComputeSetsMapUsedStatus(false, 0, 3, true, false));
	EXPECT_TRUE(ComputeSetsMapUsedStatus(false, 2, 3, false, true));
}

TEST(SetsMapUsedStatus, AnEntryAlreadySoIsLeftAlone)
{
	EXPECT_FALSE(ComputeSetsMapUsedStatus(false, 1, 3, true, true));
	EXPECT_FALSE(ComputeSetsMapUsedStatus(false, 1, 3, false, false));
}

TEST(SetsMapUsedStatus, APositionPastTheEndOrBeforeTheFirstChangesNothing)
{
	EXPECT_FALSE(ComputeSetsMapUsedStatus(false, 3, 3, true, false));
	// [rc4l] A script's position 0 is one below the first entry, which wraps past the end.
	EXPECT_FALSE(ComputeSetsMapUsedStatus(false, 0u - 1, 3, true, false));
}

TEST(SetsMapUsedStatus, AClientNeverChangesItItself)
{
	EXPECT_FALSE(ComputeSetsMapUsedStatus(true, 0, 3, true, false));
}
