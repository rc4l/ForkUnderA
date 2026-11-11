// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/dev-mode/computation/devmode_compute.h"

#include <gtest/gtest.h>

using namespace zx::devmode;

TEST(ComputeDevModeAllowed, OnlyAClientOfAServerWithoutCheatsIsRefused)
{
	EXPECT_TRUE(ComputeDevModeAllowed(false, false));   // [rc4l] offline, a demo, or a server's own console
	EXPECT_TRUE(ComputeDevModeAllowed(false, true));
	EXPECT_FALSE(ComputeDevModeAllowed(true, false));   // [rc4l] a player on a normal server
	EXPECT_TRUE(ComputeDevModeAllowed(true, true));     // [rc4l] a server that allows cheats
}

TEST(ComputeDevModeChange, HoldsOnClosingAndReleasesOnOpening)
{
	EXPECT_EQ(kDevModeStay, ComputeDevModeChange(true, true));
	EXPECT_EQ(kDevModeStay, ComputeDevModeChange(false, false));
	EXPECT_EQ(kDevModeHold, ComputeDevModeChange(true, false));
	EXPECT_EQ(kDevModeRelease, ComputeDevModeChange(false, true));
}

TEST(ComputeDevModeHolds, OnlyASettingOffItsDefault)
{
	EXPECT_TRUE(ComputeDevModeHolds("0", "1"));
	EXPECT_FALSE(ComputeDevModeHolds("1", "1"));
	EXPECT_FALSE(ComputeDevModeHolds("", ""));
}

TEST(ComputeDevModeArchivedValue, AHeldSettingSavesThePlayersValue)
{
	EXPECT_STREQ("0", ComputeDevModeArchivedValue("0", "1"));
	EXPECT_STREQ("1", ComputeDevModeArchivedValue(NULL, "1"));
}
