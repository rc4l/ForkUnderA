// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "madeinplay_compute.h"

#include <gtest/gtest.h>

using namespace zx::hwrender;

namespace {

MadeInPlay Armed()
{
	MadeInPlay m;
	for (int i = 0; i < kMadeInPlayArmFrames; i++) ComputeMadeInPlayFrame(m, true);
	return m;
}

} // namespace

// [rc4l] The first floor mark compiled its pipelines mid-fight, a 42 ms frame nothing reported.
TEST(MadeInPlay, WhatALevelMakesWhileLoadingIsNotCounted)
{
	MadeInPlay m;
	EXPECT_FALSE(ComputeMadeInPlayNote(m, "pipeline", "scene"));
	ComputeMadeInPlayFrame(m, true);
	EXPECT_FALSE(ComputeMadeInPlayNote(m, "pipeline", "scene"));
	ComputeMadeInPlayFrame(m, true);
	EXPECT_TRUE(ComputeMadeInPlayNote(m, "pipeline", "decal"));
	EXPECT_EQ(1, m.total);
	ASSERT_EQ(1u, m.entries.size());
	EXPECT_EQ(kMadeInPlayArmFrames, m.entries[0].firstFrame);
}

TEST(MadeInPlay, ALevelNotReadyStartsTheCountAgain)
{
	MadeInPlay m = Armed();
	ComputeMadeInPlayFrame(m, false);
	EXPECT_EQ(0, m.readyFrames);
	EXPECT_FALSE(ComputeMadeInPlayNote(m, "texture", "x"));
}

TEST(MadeInPlay, TheSameKindAndNameAddUpAndANameless)
{
	MadeInPlay m = Armed();
	ComputeMadeInPlayNote(m, "texture", "BLOODA0");
	ComputeMadeInPlayNote(m, "texture", "BLOODA0");
	ComputeMadeInPlayNote(m, "buffer", "BLOODA0");
	ComputeMadeInPlayNote(m, "buffer", nullptr);
	ComputeMadeInPlayNote(m, "buffer", "");
	EXPECT_EQ(5, m.total);
	ASSERT_EQ(3u, m.entries.size());
	EXPECT_EQ(2, m.entries[0].count);
	EXPECT_EQ("(unnamed)", m.entries[2].name);
	EXPECT_EQ(2, m.entries[2].count);
}

TEST(MadeInPlay, EntriesAreBoundedWhileTheTotalKeepsCounting)
{
	MadeInPlay m = Armed();
	for (size_t i = 0; i < kMadeInPlayMaxEntries + 3; i++) ComputeMadeInPlayNote(m, "texture", std::to_string(i).c_str());
	EXPECT_EQ(kMadeInPlayMaxEntries, m.entries.size());
	EXPECT_EQ((int)kMadeInPlayMaxEntries + 3, m.total);
	EXPECT_NE(std::string::npos, ComputeMadeInPlayReport(m).find("(more not listed)"));
}

TEST(MadeInPlay, TheReportListsEachWithItsCountAndFrameAndClearForgetsThem)
{
	MadeInPlay m = Armed();
	ComputeMadeInPlayNote(m, "pipeline", "deferred decals");
	const std::string r = ComputeMadeInPlayReport(m);
	EXPECT_EQ(0u, r.find("1 GPU objects made in play\n"));
	EXPECT_NE(std::string::npos, r.find("pipeline"));
	EXPECT_NE(std::string::npos, r.find("deferred decals"));
	EXPECT_EQ(std::string::npos, r.find("more not listed"));
	ComputeMadeInPlayClear(m);
	EXPECT_EQ(0, m.total);
	EXPECT_TRUE(m.entries.empty());
	EXPECT_EQ("0 GPU objects made in play\n", ComputeMadeInPlayReport(m));
}
