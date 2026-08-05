// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/joinplan_compute.h"

using zx::ComputeJoinWadList;
using std::string;
using std::vector;

TEST(JoinWadList, PassesACleanListThroughUnchanged)
{
	const vector<string> pwads = { "brutal.wad", "maps.wad", "music.pk3" };
	EXPECT_EQ(pwads, ComputeJoinWadList("doom2.wad", pwads));
}

TEST(JoinWadList, PreservesOrderBecauseLoadOrderIsSemantic)
{
	// Later files override earlier ones, so the server's ordering IS the mod. Sorting or reversing
	// this would load a different game while still "finding every file".
	const vector<string> pwads = { "zzz.wad", "aaa.wad", "mmm.wad" };
	const vector<string> got = ComputeJoinWadList("", pwads);
	ASSERT_EQ(3u, got.size());
	EXPECT_EQ("zzz.wad", got[0]);
	EXPECT_EQ("aaa.wad", got[1]);
	EXPECT_EQ("mmm.wad", got[2]);
}

TEST(JoinWadList, DropsTheIwadWhenItIsAlsoListedAsAPwad)
{
	const vector<string> pwads = { "doom2.wad", "brutal.wad" };
	const vector<string> got = ComputeJoinWadList("doom2.wad", pwads);
	ASSERT_EQ(1u, got.size());
	EXPECT_EQ("brutal.wad", got[0]);
}

TEST(JoinWadList, IwadMatchIsCaseInsensitive)
{
	// A Linux server advertising a different case must not make us load the IWAD as a mod.
	const vector<string> pwads = { "DOOM2.WAD", "brutal.wad" };
	const vector<string> got = ComputeJoinWadList("doom2.wad", pwads);
	ASSERT_EQ(1u, got.size());
	EXPECT_EQ("brutal.wad", got[0]);
}

TEST(JoinWadList, DropsLaterDuplicatesKeepingTheFirst)
{
	const vector<string> pwads = { "a.wad", "b.wad", "A.WAD", "b.wad", "c.wad" };
	const vector<string> got = ComputeJoinWadList("", pwads);
	ASSERT_EQ(3u, got.size());
	EXPECT_EQ("a.wad", got[0]);		// the first spelling is what we keep
	EXPECT_EQ("b.wad", got[1]);
	EXPECT_EQ("c.wad", got[2]);
}

TEST(JoinWadList, DropsBlankEntriesRatherThanFailingTheJoin)
{
	const vector<string> pwads = { "a.wad", "", "   ", "\t", "b.wad" };
	const vector<string> got = ComputeJoinWadList("doom2.wad", pwads);
	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("a.wad", got[0]);
	EXPECT_EQ("b.wad", got[1]);
}

TEST(JoinWadList, AnEmptyIwadSuppressesNothing)
{
	// Guards the obvious bug: folding "" and comparing it against every name would eat the list.
	const vector<string> pwads = { "a.wad", "b.wad" };
	EXPECT_EQ(pwads, ComputeJoinWadList("", pwads));
	EXPECT_EQ(pwads, ComputeJoinWadList("   ", pwads));
}

TEST(JoinWadList, EmptyInputGivesEmptyOutput)
{
	EXPECT_TRUE(ComputeJoinWadList("doom2.wad", {}).empty());
	EXPECT_TRUE(ComputeJoinWadList("", {}).empty());
	EXPECT_TRUE(ComputeJoinWadList("", { "", "  " }).empty());
}

TEST(JoinWadList, KeepsTheServersSpellingForTheFileSearch)
{
	// Case folding decides equality only; the name handed back must be what the server said, since
	// that is what gets looked up on disk.
	const vector<string> pwads = { "BrutalDoom.WAD" };
	const vector<string> got = ComputeJoinWadList("", pwads);
	ASSERT_EQ(1u, got.size());
	EXPECT_EQ("BrutalDoom.WAD", got[0]);
}
