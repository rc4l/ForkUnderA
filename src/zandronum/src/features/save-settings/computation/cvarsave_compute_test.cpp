// [MobiusU9B] Tests for which cvar value a save keeps and when a server's change reaches the client, covering every branch of cvarsave_compute.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/save-settings/computation/cvarsave_compute.h"

#include <gtest/gtest.h>

#include <string>

using zx::ComputeArchivedCVarValue;
using zx::ComputeMirrorsToOwnCVar;
using zx::ComputeRecordsPreScriptValue;
using zx::ComputeResetChangesCVar;
using zx::ComputeSendsUserCVarChange;

TEST(ComputeRecordsPreScriptValue, RecordsAnEngineCVarAScriptChanges)
{
	EXPECT_TRUE(ComputeRecordsPreScriptValue(true, false, "0", "1"));
}

TEST(ComputeRecordsPreScriptValue, NeverRecordsAModsOwnCVar)
{
	EXPECT_FALSE(ComputeRecordsPreScriptValue(false, false, "0", "1"));
}

TEST(ComputeRecordsPreScriptValue, KeepsTheFirstRecordedValue)
{
	// [MobiusU9B] A second script change must not replace the player's own value with the first script's.
	EXPECT_FALSE(ComputeRecordsPreScriptValue(true, true, "1", "2"));
}

TEST(ComputeRecordsPreScriptValue, SkipsASetToTheSameValue)
{
	EXPECT_FALSE(ComputeRecordsPreScriptValue(true, false, "1", "1"));
	EXPECT_FALSE(ComputeRecordsPreScriptValue(true, false, nullptr, ""));
}

TEST(ComputeRecordsPreScriptValue, ReadsNullAsEmpty)
{
	EXPECT_TRUE(ComputeRecordsPreScriptValue(true, false, nullptr, "x"));
	EXPECT_TRUE(ComputeRecordsPreScriptValue(true, false, "x", nullptr));
}

TEST(ComputeArchivedCVarValue, WritesThePreScriptValueOfAGuardedCVar)
{
	EXPECT_EQ(std::string("0"), ComputeArchivedCVarValue(true, "1", true, "0"));
}

TEST(ComputeArchivedCVarValue, WritesAnEmptyPreScriptValueStoredAsNull)
{
	EXPECT_EQ(std::string(""), ComputeArchivedCVarValue(true, "player", true, nullptr));
}

TEST(ComputeArchivedCVarValue, WritesTheLiveValueWhenNothingWasRecorded)
{
	EXPECT_EQ(std::string("1"), ComputeArchivedCVarValue(true, "1", false, nullptr));
}

TEST(ComputeArchivedCVarValue, WritesTheLiveValueWhenProtectionIsOff)
{
	EXPECT_EQ(std::string("1"), ComputeArchivedCVarValue(false, "1", true, "0"));
}

TEST(ComputeArchivedCVarValue, WritesAnEmptyStringForANullLiveValue)
{
	EXPECT_EQ(std::string(""), ComputeArchivedCVarValue(false, nullptr, false, nullptr));
}

TEST(ComputeResetChangesCVar, SetsACVarThatChangedSinceTheSave)
{
	EXPECT_TRUE(ComputeResetChangesCVar("2", "0"));
}

TEST(ComputeResetChangesCVar, LeavesACVarStillAtItsSavedValue)
{
	// [MobiusU9B] Setting gl_wallmesh to the value it had ran its callback, which dropped every floor and ceiling.
	EXPECT_FALSE(ComputeResetChangesCVar("true", "true"));
	EXPECT_FALSE(ComputeResetChangesCVar(nullptr, ""));
}

TEST(ComputeSendsUserCVarChange, AServerSendsAChangedValue)
{
	EXPECT_TRUE(ComputeSendsUserCVarChange(true, "0", "1"));
}

TEST(ComputeSendsUserCVarChange, AServerSendsNothingForTheSameValue)
{
	EXPECT_FALSE(ComputeSendsUserCVarChange(true, "1", "1"));
}

TEST(ComputeSendsUserCVarChange, AClientOrOfflineGameSendsNothing)
{
	EXPECT_FALSE(ComputeSendsUserCVarChange(false, "0", "1"));
}

TEST(ComputeMirrorsToOwnCVar, MirrorsYourOwnModUserCVar)
{
	EXPECT_TRUE(ComputeMirrorsToOwnCVar(true, false, true, false, "0", "1"));
}

TEST(ComputeMirrorsToOwnCVar, LeavesAnotherPlayersValueToTheirCopy)
{
	EXPECT_FALSE(ComputeMirrorsToOwnCVar(false, false, true, false, "0", "1"));
}

TEST(ComputeMirrorsToOwnCVar, LeavesYourCVarAloneDuringADemo)
{
	// [MobiusU9B] A client demo replays the recording player's settings, which must not land in the viewer's ini.
	EXPECT_FALSE(ComputeMirrorsToOwnCVar(true, true, true, false, "0", "1"));
}

TEST(ComputeMirrorsToOwnCVar, LeavesEngineUserinfoAlone)
{
	EXPECT_FALSE(ComputeMirrorsToOwnCVar(true, false, false, false, "0", "1"));
}

TEST(ComputeMirrorsToOwnCVar, LeavesAnIgnoredCVarAlone)
{
	EXPECT_FALSE(ComputeMirrorsToOwnCVar(true, false, true, true, "0", "1"));
}

TEST(ComputeMirrorsToOwnCVar, SkipsAnUnchangedValue)
{
	EXPECT_FALSE(ComputeMirrorsToOwnCVar(true, false, true, false, "1", "1"));
}
