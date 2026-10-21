// [MobiusU9B] Tests for the save-settings popup text, covering every branch of savesettings_compute.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/save-settings/computation/savesettings_compute.h"

#include <gtest/gtest.h>

using zx::ComputeSaveSettingsMessage;

TEST(ComputeSaveSettingsMessage, ReportsSuccess)
{
	EXPECT_EQ("Settings saved.", ComputeSaveSettingsMessage(true, nullptr));
}

TEST(ComputeSaveSettingsMessage, IgnoresTheReasonOnSuccess)
{
	// [MobiusU9B] errno can hold a stale value after a successful write, which must not leak into the popup.
	EXPECT_EQ("Settings saved.", ComputeSaveSettingsMessage(true, "Permission denied"));
}

TEST(ComputeSaveSettingsMessage, ReportsTheReasonOnFailure)
{
	EXPECT_EQ("Could not save settings:\nPermission denied",
		ComputeSaveSettingsMessage(false, "Permission denied"));
}

TEST(ComputeSaveSettingsMessage, FallsBackWhenThereIsNoReason)
{
	EXPECT_EQ("Could not save settings:\nunknown error", ComputeSaveSettingsMessage(false, nullptr));
}

TEST(ComputeSaveSettingsMessage, FallsBackWhenTheReasonIsEmpty)
{
	EXPECT_EQ("Could not save settings:\nunknown error", ComputeSaveSettingsMessage(false, ""));
}
