// [rc4l] Tests for the pure crash-report decision logic (crash_report_compute).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "features/crashreport/computation/crash_report_compute.h"

using namespace zx;

// --- startup action: on/off (auto-send, no prompt) --------------------------

TEST(CrashStartup, OffCvarRevokes)
{
	EXPECT_EQ(ComputeStartupAction(0), StartupAction::RevokeConsent);
	EXPECT_EQ(ComputeStartupAction(-1), StartupAction::RevokeConsent);
}

TEST(CrashStartup, OnCvarGives)
{
	EXPECT_EQ(ComputeStartupAction(1), StartupAction::GiveConsent);
	// Legacy inis may still hold 2 (the old "always"); any positive value means on.
	EXPECT_EQ(ComputeStartupAction(2), StartupAction::GiveConsent);
	EXPECT_EQ(ComputeStartupAction(5), StartupAction::GiveConsent);
}

// --- privacy: file labels must not leak the player's home path --------------

TEST(CrashSafeLabel, StripsWindowsUserPath)
{
	EXPECT_EQ(ComputeSafeFileLabel("C:\\Users\\aurat\\wads\\lightscape.pk3"), "lightscape.pk3");
}

TEST(CrashSafeLabel, StripsUnixHomePath)
{
	EXPECT_EQ(ComputeSafeFileLabel("/home/bob/doom/mymod.pk3"), "mymod.pk3");
	EXPECT_EQ(ComputeSafeFileLabel("/Users/bob/Doom/iwad.wad"), "iwad.wad");
}

TEST(CrashSafeLabel, BareNameIsUnchanged)
{
	EXPECT_EQ(ComputeSafeFileLabel("zandronum.pk3"), "zandronum.pk3");
}

TEST(CrashSafeLabel, EmptyStaysEmpty)
{
	EXPECT_EQ(ComputeSafeFileLabel(""), "");
}
