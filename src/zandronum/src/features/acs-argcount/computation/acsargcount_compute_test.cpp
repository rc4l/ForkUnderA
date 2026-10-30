// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-argcount/computation/acsargcount_compute.h"

#include <cstring>
#include <gtest/gtest.h>

using namespace zx::acsargcount;

namespace {

// [Doruk-Aksoy] How many arguments a call must pass to the function, or 0 when it checks every one it reads.
int Needed(int funcIndex)
{
	const FunctionArgs *entry = ComputeFunctionArgs(funcIndex);
	return entry != nullptr ? entry->needed : 0;
}

} // namespace

TEST(AcsArgCount, SpawnForcedNeedsItsClassAndPositionButNotItsTidOrAngle)
{
	const FunctionArgs *shortCall = ComputeShortCall(36, 3);
	ASSERT_NE(shortCall, nullptr);
	EXPECT_EQ(shortCall->needed, 4);
	EXPECT_STREQ(shortCall->name, "SpawnForced");
	EXPECT_EQ(ComputeShortCall(36, 4), nullptr);
	EXPECT_EQ(ComputeShortCall(36, 5), nullptr);
	EXPECT_EQ(ComputeShortCall(36, 6), nullptr);
}

TEST(AcsArgCount, SpawnSpotForcedTakesTheTwoArgumentsBccLetsAScriptPass)
{
	// [Doruk-Aksoy] Upstream needs 4 and 3, but BCC declares the TID and angle optional, as DnD is compiled.
	EXPECT_EQ(Needed(20), 2);
	EXPECT_EQ(Needed(21), 2);
	EXPECT_NE(ComputeShortCall(20, 1), nullptr);
	EXPECT_EQ(ComputeShortCall(20, 2), nullptr);
	EXPECT_EQ(ComputeShortCall(21, 2), nullptr);
}

TEST(AcsArgCount, NamedScriptFunctionsNeedOnlyTheName)
{
	for (int index = 39; index <= 45; index++)
	{
		EXPECT_EQ(Needed(index), 1) << index;
		EXPECT_NE(ComputeShortCall(index, 0), nullptr) << index;
		EXPECT_EQ(ComputeShortCall(index, 1), nullptr) << index;
	}
}

TEST(AcsArgCount, ZandronumFunctionsNeedTheirDeclaredMinimum)
{
	EXPECT_EQ(Needed(106), 2);   // [Doruk-Aksoy] ForceToSpectate, zspecial.acs's KickFromGame
	EXPECT_EQ(Needed(162), 3);   // [Doruk-Aksoy] LumpReadArray's four forms, which ACC builds in
	EXPECT_EQ(Needed(165), 3);
	EXPECT_EQ(Needed(177), 2);   // [Doruk-Aksoy] GetPlayerCountry
	EXPECT_EQ(Needed(185), 2);   // [Doruk-Aksoy] IsPlayerContestingControlPoint
}

TEST(AcsArgCount, FunctionsThatCheckEveryArgumentAreNeverShort)
{
	// [Doruk-Aksoy] UniqueTID, SetHUDClipRect, SetHUDWrapWidth, GetWeapon, ResetMap, GetMapPosition, AddBot and a shader function.
	const int indices[] = { 46, 51, 52, 69, 100, 151, 168, 24000 };
	for (int index : indices)
	{
		EXPECT_EQ(ComputeFunctionArgs(index), nullptr) << index;
		EXPECT_EQ(ComputeShortCall(index, 0), nullptr) << index;
	}
}

TEST(AcsArgCount, IndicesNoFunctionHoldsAreNeverShort)
{
	const int indices[] = { -1, 0, 255, 256, 300, 19619, 19621, 30000 };
	for (int index : indices) EXPECT_EQ(ComputeShortCall(index, 0), nullptr) << index;
}

TEST(AcsArgCount, ZDaemonsGetTeamScoreIsFoundPastTheDirectRange)
{
	const FunctionArgs *shortCall = ComputeShortCall(19620, 0);
	ASSERT_NE(shortCall, nullptr);
	EXPECT_EQ(shortCall->needed, 1);
	EXPECT_STREQ(shortCall->name, "GetTeamScore");
	EXPECT_EQ(ComputeShortCall(19620, 1), nullptr);
}

TEST(AcsArgCount, TableAscendsAndEveryEntryIsFoundAndJudgedAtItsCount)
{
	ASSERT_GT(kFunctionArgsCount, 0);
	for (int i = 0; i < kFunctionArgsCount; i++)
	{
		const FunctionArgs &entry = kFunctionArgs[i];
		if (i > 0) EXPECT_LT(kFunctionArgs[i - 1].index, entry.index) << entry.name;
		// [Doruk-Aksoy] QuakeEx's 8 is the most any function needs.
		EXPECT_GE(entry.needed, 1) << entry.name;
		EXPECT_LE(entry.needed, 8) << entry.name;
		ASSERT_NE(entry.name, nullptr);
		EXPECT_GT(std::strlen(entry.name), 0u);
		EXPECT_EQ(ComputeFunctionArgs(entry.index), &entry) << entry.name;
		EXPECT_EQ(ComputeShortCall(entry.index, entry.needed - 1), &entry) << entry.name;
		EXPECT_EQ(ComputeShortCall(entry.index, entry.needed), nullptr) << entry.name;
	}
}
