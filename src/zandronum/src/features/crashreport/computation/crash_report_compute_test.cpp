// [rc4l] Tests for the pure crash-report decision logic (crash_report_compute).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "features/crashreport/computation/crash_report_compute.h"

using namespace zx;

// --- startup action: cvar x crashed matrix ----------------------------------

TEST(CrashStartup, NeverCvarAlwaysRevokes)
{
	EXPECT_EQ(ComputeStartupAction(0, true), StartupAction::RevokeConsent);
	EXPECT_EQ(ComputeStartupAction(0, false), StartupAction::RevokeConsent);
}

TEST(CrashStartup, AlwaysCvarAlwaysGives)
{
	EXPECT_EQ(ComputeStartupAction(2, true), StartupAction::GiveConsent);
	EXPECT_EQ(ComputeStartupAction(2, false), StartupAction::GiveConsent);
	EXPECT_EQ(ComputeStartupAction(5, true), StartupAction::GiveConsent) << "any >=2 means always";
}

TEST(CrashStartup, AskCvarPromptsOnlyWhenCrashed)
{
	EXPECT_EQ(ComputeStartupAction(1, true), StartupAction::ShowPrompt);
	EXPECT_EQ(ComputeStartupAction(1, false), StartupAction::Nothing);
}

// --- prompt choices: no permanent "never send" -------------------------------

TEST(CrashChoiceMap, SendOnceUploadsButKeepsAsking)
{
	const CrashChoiceAction a = ComputeChoiceAction(CrashChoice::SendOnce);
	EXPECT_TRUE(a.upload);
	EXPECT_FALSE(a.persistAlways) << "one-time send must not silence future prompts";
	EXPECT_FALSE(a.saveToDisk);
}

TEST(CrashChoiceMap, AlwaysSendUploadsAndSilencesPrompts)
{
	const CrashChoiceAction a = ComputeChoiceAction(CrashChoice::AlwaysSend);
	EXPECT_TRUE(a.upload);
	EXPECT_TRUE(a.persistAlways);
	EXPECT_FALSE(a.saveToDisk);
}

TEST(CrashChoiceMap, SaveToDiskNeverUploads)
{
	const CrashChoiceAction a = ComputeChoiceAction(CrashChoice::SaveToDisk);
	EXPECT_FALSE(a.upload);
	EXPECT_FALSE(a.persistAlways);
	EXPECT_TRUE(a.saveToDisk);
}

TEST(CrashChoiceMap, NotNowDoesNothingPersistent)
{
	const CrashChoiceAction a = ComputeChoiceAction(CrashChoice::NotNow);
	EXPECT_FALSE(a.upload);
	EXPECT_FALSE(a.persistAlways) << "declining once must still ask next crash (no silent opt-out)";
	EXPECT_FALSE(a.saveToDisk);
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
