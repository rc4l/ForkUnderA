// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/rendersettings/computation/forcedvideo_compute.h"

#include <gtest/gtest.h>

using namespace zx::rendersettings;

TEST(ForcedVideo, ASettingReadsItsDefaultOnlyWhileTheServerForcesThem)
{
	EXPECT_EQ(20, ComputeForcedVideoInt(true, 64, 20));
	EXPECT_EQ(64, ComputeForcedVideoInt(false, 64, 20));
}

TEST(ForcedVideo, ForcedDefaultsNeverTurnFogOff)
{
	EXPECT_EQ(1, ComputeForcedFogMode(true, 0));
	EXPECT_EQ(2, ComputeForcedFogMode(true, 2));
	EXPECT_EQ(0, ComputeForcedFogMode(false, 0));
}
