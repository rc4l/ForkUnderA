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
	EXPECT_TRUE(a.flush) << "must wait for delivery, else the async send is lost on exit (v0.1.8 bug)";
}

TEST(CrashChoiceMap, AlwaysSendUploadsAndSilencesPrompts)
{
	const CrashChoiceAction a = ComputeChoiceAction(CrashChoice::AlwaysSend);
	EXPECT_TRUE(a.upload);
	EXPECT_TRUE(a.persistAlways);
	EXPECT_FALSE(a.saveToDisk);
	EXPECT_TRUE(a.flush) << "must wait for delivery, else the async send is lost on exit (v0.1.8 bug)";
}

// Regression guard for the v0.1.8 bug: a choice that uploads MUST also flush, otherwise the report
// is queued to the background transport and lost when the process exits right after consent.
TEST(CrashChoiceMap, UploadAlwaysImpliesFlush)
{
	const CrashChoice all[] = { CrashChoice::SendOnce, CrashChoice::AlwaysSend,
							   CrashChoice::SaveToDisk, CrashChoice::NotNow };
	for (CrashChoice c : all)
	{
		const CrashChoiceAction a = ComputeChoiceAction(c);
		if (a.upload)
			EXPECT_TRUE(a.flush) << "an uploading choice must flush to guarantee delivery";
		else
			EXPECT_FALSE(a.flush) << "no upload -> nothing to flush";
	}
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
