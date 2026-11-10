// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/net-session/computation/levelauth_compute.h"

#include <gtest/gtest.h>

using namespace zx::netsession;

TEST(ComputeLevelAuthVerdict, TheMapAskedForJudgesTheChecksum)
{
	EXPECT_EQ(ComputeLevelAuthVerdict(true, "MAP01", "MAP01", true), LevelAuthVerdict::Accept);
	EXPECT_EQ(ComputeLevelAuthVerdict(true, "MAP01", "MAP01", false), LevelAuthVerdict::RejectLevel);
}

TEST(ComputeLevelAuthVerdict, AMapChangedSinceAskingAsksAgain)
{
	// [Doruk-Aksoy] DnD's setsoftcore: asked for VR, answered after the server moved to map01.
	EXPECT_EQ(ComputeLevelAuthVerdict(true, "VR", "map01", false), LevelAuthVerdict::AskAgain);
	EXPECT_EQ(ComputeLevelAuthVerdict(true, "VR", "map01", true), LevelAuthVerdict::AskAgain);
	EXPECT_EQ(ComputeLevelAuthVerdict(true, "MAP01", "MAP010", false), LevelAuthVerdict::AskAgain);
	EXPECT_EQ(ComputeLevelAuthVerdict(true, "MAP01", nullptr, false), LevelAuthVerdict::AskAgain);
}

TEST(ComputeLevelAuthVerdict, MapNamesCompareWithoutCase)
{
	EXPECT_EQ(ComputeLevelAuthVerdict(true, "map01", "MAP01", true), LevelAuthVerdict::Accept);
	EXPECT_EQ(ComputeLevelAuthVerdict(true, "map01", "MAP01", false), LevelAuthVerdict::RejectLevel);
}

TEST(ComputeLevelAuthVerdict, NoMapOnRecordJudgesTheChecksumAsBefore)
{
	EXPECT_EQ(ComputeLevelAuthVerdict(true, "", "MAP01", false), LevelAuthVerdict::RejectLevel);
	EXPECT_EQ(ComputeLevelAuthVerdict(true, nullptr, "MAP01", true), LevelAuthVerdict::Accept);
}

TEST(ComputeLevelAuthVerdict, AnIdentityThatFailsIsRefusedFirst)
{
	EXPECT_EQ(ComputeLevelAuthVerdict(false, "MAP01", "MAP01", true), LevelAuthVerdict::RejectIdentity);
	EXPECT_EQ(ComputeLevelAuthVerdict(false, "VR", "map01", false), LevelAuthVerdict::RejectIdentity);
}
