// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/computation/handoff_compute.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace zx::handoff;

namespace {
const Phase kTic = { true, false };      // [rc4l] a tic running
const Phase kHandoff = { false, true };  // [rc4l] the main thread between tics
const Phase kFrame = { false, false };   // [rc4l] the main thread drawing, the game idle
}

TEST(HandoffOwner, MainAndGameThreadSeamsFollowTheThread)
{
	EXPECT_TRUE(ComputeOwnerAllows(Owner::Main, true, false, kTic));
	EXPECT_FALSE(ComputeOwnerAllows(Owner::Main, false, true, kHandoff));
	EXPECT_TRUE(ComputeOwnerAllows(Owner::GameThread, false, true, kFrame));
	EXPECT_FALSE(ComputeOwnerAllows(Owner::GameThread, true, false, kTic));
}

TEST(HandoffOwner, GameDataBelongsToTheRunningTicOrToTheHandoff)
{
	EXPECT_TRUE(ComputeOwnerAllows(Owner::GameData, false, true, kTic));
	EXPECT_FALSE(ComputeOwnerAllows(Owner::GameData, false, true, kHandoff));
	EXPECT_TRUE(ComputeOwnerAllows(Owner::GameData, true, false, kHandoff));
	EXPECT_FALSE(ComputeOwnerAllows(Owner::GameData, true, false, kTic));
	// [rc4l] An inline tic is the main thread running the game; drawing afterwards is outside the handoff, as beside a running game.
	EXPECT_TRUE(ComputeOwnerAllows(Owner::GameData, true, true, kTic));
	EXPECT_FALSE(ComputeOwnerAllows(Owner::GameData, true, true, kFrame));
}

TEST(HandoffOwner, TheHandoffIsTheMainThreadBetweenTics)
{
	EXPECT_TRUE(ComputeOwnerAllows(Owner::Handoff, true, true, kHandoff));
	EXPECT_FALSE(ComputeOwnerAllows(Owner::Handoff, true, true, kTic));
	EXPECT_FALSE(ComputeOwnerAllows(Owner::Handoff, true, false, kFrame));
	EXPECT_FALSE(ComputeOwnerAllows(Owner::Handoff, false, true, kHandoff));
	EXPECT_TRUE(ComputeOwnerAllows(Owner::Any, false, false, kTic));
}

TEST(HandoffOwner, IdleIsTheMainThreadWhileNoTicRuns)
{
	EXPECT_TRUE(ComputeOwnerAllows(Owner::Idle, true, true, kHandoff));
	EXPECT_TRUE(ComputeOwnerAllows(Owner::Idle, true, false, kFrame));
	EXPECT_FALSE(ComputeOwnerAllows(Owner::Idle, true, true, kTic));
	EXPECT_FALSE(ComputeOwnerAllows(Owner::Idle, false, true, kFrame));
}

// [rc4l] Only the main thread waits, only while a tic runs on the game thread; an inline tic's main thread is its game thread too.
TEST(HandoffBesideTic, TheMainThreadWaitsOnlyBesideATicOnTheGameThread)
{
	EXPECT_TRUE(ComputeBesideTic(true, false, Phase{ true, false }));
	EXPECT_FALSE(ComputeBesideTic(true, false, Phase{ false, true }));
	EXPECT_FALSE(ComputeBesideTic(true, true, Phase{ true, false }));
	EXPECT_FALSE(ComputeBesideTic(false, true, Phase{ true, false }));
}

TEST(HandoffOwner, EveryOwnerHasAName)
{
	EXPECT_STREQ("main", ComputeOwnerName(Owner::Main));
	EXPECT_STREQ("game", ComputeOwnerName(Owner::GameThread));
	EXPECT_STREQ("game data", ComputeOwnerName(Owner::GameData));
	EXPECT_STREQ("handoff", ComputeOwnerName(Owner::Handoff));
	EXPECT_STREQ("idle", ComputeOwnerName(Owner::Idle));
	EXPECT_STREQ("any", ComputeOwnerName(Owner::Any));
}

TEST(HandoffStress, NoMaximumMeansNoDelay)
{
	for (uint64_t i = 0; i < 64; i++) EXPECT_EQ(0u, ComputeStressDelayUs(7, 3, i, 0));
}

TEST(HandoffStress, AboutOneEntryInEightWaitsAndTheSameInputsWaitTheSame)
{
	int delayed = 0;
	std::set<uint32_t> lengths;
	for (uint64_t i = 0; i < 8000; i++)
	{
		const uint32_t us = ComputeStressDelayUs(42, 5, i, 500);
		EXPECT_LE(us, 500u);
		EXPECT_EQ(us, ComputeStressDelayUs(42, 5, i, 500));
		if (us > 0) { delayed++; lengths.insert(us); }
	}
	EXPECT_GT(delayed, 700);
	EXPECT_LT(delayed, 1300);
	EXPECT_GT(lengths.size(), 100u);
}

TEST(HandoffStress, AnotherSeedOrSeamDelaysOtherEntries)
{
	int differ = 0;
	for (uint64_t i = 0; i < 200; i++)
	{
		if (ComputeStressDelayUs(1, 0, i, 1000) != ComputeStressDelayUs(2, 0, i, 1000)) differ++;
		if (ComputeStressDelayUs(1, 0, i, 1000) != ComputeStressDelayUs(1, 1, i, 1000)) differ++;
	}
	EXPECT_GT(differ, 20);
}

TEST(SeamRegistry, ANameIsDeclaredOnceAndAnotherOwnerConflicts)
{
	SeamRegistry r;
	const int a = r.Declare("copy/fill", Owner::Handoff);
	const int b = r.Declare("sound/device", Owner::Main);
	EXPECT_EQ(0, a);
	EXPECT_EQ(1, b);
	EXPECT_EQ(a, r.Declare("copy/fill", Owner::Handoff));
	EXPECT_EQ(-1, r.Declare("copy/fill", Owner::Main));
	EXPECT_EQ(2, r.Size());
	EXPECT_EQ("sound/device", r.At(b).name);
	EXPECT_EQ(Owner::Main, r.At(b).owner);
}

TEST(SeamRegistry, EntriesAndViolationsAreCounted)
{
	SeamRegistry r;
	const int a = r.Declare("a/b", Owner::Main);
	const int c = r.Declare("c/d", Owner::GameData);
	r.Enter(a, true);
	r.Enter(a, false);
	r.Enter(c, false);
	EXPECT_EQ(2u, r.At(a).entries.load());
	EXPECT_EQ(1u, r.At(a).violations.load());
	EXPECT_EQ(2u, r.Violations());
	const std::string table = ComputeSeamTable(r);
	EXPECT_NE(std::string::npos, table.find("a/b"));
	EXPECT_NE(std::string::npos, table.find("game data"));
	EXPECT_NE(std::string::npos, table.find("1 violations"));
}

TEST(SeamRegistry, AnEmptyRegistryListsNothing)
{
	SeamRegistry r;
	EXPECT_EQ(0u, r.Violations());
	EXPECT_EQ("", ComputeSeamTable(r));
}
