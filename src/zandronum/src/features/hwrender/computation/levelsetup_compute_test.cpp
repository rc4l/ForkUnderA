// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/levelsetup_compute.h"

#include <gtest/gtest.h>

using namespace zx::hwrender;

TEST(LevelSetup, ANewLevelArmsThenWaitsThenUploads)
{
	LevelSetup s = ComputeLevelSetupStart();
	ComputeLevelSetupForLevel(s, 3);
	EXPECT_EQ(kSetupArm, s.state);
	ComputeLevelSetupArmed(s);
	EXPECT_EQ(kSetupWaiting, s.state);
	for (int i = 1; i < kSetupWaitFrames; i++) EXPECT_FALSE(ComputeLevelSetupWaited(s));
	EXPECT_TRUE(ComputeLevelSetupWaited(s));
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

TEST(StandaloneBake, ArmedOnceThenSettledAfterABakeAndAnUpload)
{
	StandaloneBake b = { false, 0 };
	EXPECT_FALSE(ComputeStandaloneBakeStep(b, true, false));
	EXPECT_TRUE(ComputeStandaloneBakeStep(b, true, true));
	EXPECT_FALSE(ComputeStandaloneBakeStep(b, true, true));
	EXPECT_FALSE(ComputeStandaloneBakeStep(b, true, true));
	EXPECT_FALSE(b.baked);
	EXPECT_FALSE(ComputeStandaloneBakeStep(b, true, true));
	EXPECT_TRUE(b.baked);
	EXPECT_FALSE(ComputeStandaloneBakeStep(b, true, true));
	// [rc4l] Switched off, it is forgotten and arms again when switched back on.
	EXPECT_FALSE(ComputeStandaloneBakeStep(b, false, true));
	EXPECT_FALSE(b.baked);
	EXPECT_TRUE(ComputeStandaloneBakeStep(b, true, true));
}
