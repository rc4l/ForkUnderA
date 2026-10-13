// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/levelsetup_compute.h"

#include <gtest/gtest.h>

using namespace zx::hwrender;

TEST(LevelSetup, ANewLevelBakesThenUploads)
{
	LevelSetup s = ComputeLevelSetupStart();
	ComputeLevelSetupForLevel(s, 3);
	EXPECT_EQ(kSetupArm, s.state);
	ComputeLevelSetupArmed(s);
	EXPECT_EQ(kSetupWaiting, s.state);
	ComputeLevelSetupUploaded(s, true);
	EXPECT_EQ(kSetupReady, s.state);
	// [rc4l] The same level again changes nothing; a new one starts over.
	ComputeLevelSetupForLevel(s, 3);
	EXPECT_EQ(kSetupReady, s.state);
	ComputeLevelSetupForLevel(s, 4);
	EXPECT_EQ(kSetupArm, s.state);
	EXPECT_EQ(0, s.tries);
}

TEST(LevelSetup, AFailedUploadRearmsUntilItGivesUp)
{
	LevelSetup s = ComputeLevelSetupStart();
	ComputeLevelSetupForLevel(s, 1);
	for (int i = 1; i < kSetupMaxTries; i++)
	{
		ComputeLevelSetupUploaded(s, false);
		EXPECT_EQ(kSetupArm, s.state);
	}
	ComputeLevelSetupUploaded(s, false);
	EXPECT_EQ(kSetupGaveUp, s.state);
}
