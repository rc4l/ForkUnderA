// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/dmflagnames_compute.h"

using zx::ComputeSetFlagNames;
using zx::FlagNameEntry;
using std::string;
using std::vector;

namespace
{
// The real shape of the problem, lifted from doomdef.h: three plain flags and one two-bit field
// whose three values overlap.
const FlagNameEntry kTable[] = {
	{ 0, 1,  "NO HEALTH" },
	{ 0, 2,  "NO ITEMS" },
	{ 0, 4,  "WEAPONS STAY" },
	{ 0, 8,  "FORCE FALLINGZD" },		// 1 << 3
	{ 0, 16, "FORCE FALLINGHX" },		// 2 << 3
	{ 0, 24, "FORCE FALLINGST" },		// 3 << 3 -- both bits
	{ 1, 1,  "YES DEGENERATION" },
	{ 2, 1,  "NO ROCKET JUMPING" },
};
const int kCount = static_cast<int>(sizeof kTable / sizeof kTable[0]);
} // namespace

TEST(SetFlagNames, ListsThePlainFlagsThatAreOn)
{
	const vector<string> got = ComputeSetFlagNames(kTable, kCount, { 1 | 4, 0, 0 });
	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("NO HEALTH", got[0]);
	EXPECT_EQ("WEAPONS STAY", got[1]);
}

TEST(SetFlagNames, AMultiBitFieldNamesOnlyItself)
{
	// The bug this unit exists for. Strife falling is 3 << 3, so bits 3 AND 4 are set -- a plain AND
	// would report all three falling styles at once on a server that has exactly one.
	const vector<string> got = ComputeSetFlagNames(kTable, kCount, { 24, 0, 0 });
	ASSERT_EQ(1u, got.size());
	EXPECT_EQ("FORCE FALLINGST", got[0]);
}

TEST(SetFlagNames, TheNarrowValuesOfThatFieldStillWorkAlone)
{
	const vector<string> zd = ComputeSetFlagNames(kTable, kCount, { 8, 0, 0 });
	ASSERT_EQ(1u, zd.size());
	EXPECT_EQ("FORCE FALLINGZD", zd[0]);

	const vector<string> hx = ComputeSetFlagNames(kTable, kCount, { 16, 0, 0 });
	ASSERT_EQ(1u, hx.size());
	EXPECT_EQ("FORCE FALLINGHX", hx[0]);
}

TEST(SetFlagNames, AFieldDoesNotSwallowUnrelatedFlagsInTheSameWord)
{
	// Claiming bits must be scoped to the field, not to the word.
	const vector<string> got = ComputeSetFlagNames(kTable, kCount, { 24 | 1, 0, 0 });
	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("NO HEALTH", got[0]);
	EXPECT_EQ("FORCE FALLINGST", got[1]);
}

TEST(SetFlagNames, ReadsEachWordSeparately)
{
	// Bit 1 in three different words is three different flags; matching them against the wrong word
	// is the other easy way to report nonsense.
	const vector<string> got = ComputeSetFlagNames(kTable, kCount, { 0, 1, 1 });
	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("YES DEGENERATION", got[0]);
	EXPECT_EQ("NO ROCKET JUMPING", got[1]);
}

TEST(SetFlagNames, EmitsInTableOrderNotMatchOrder)
{
	// Matching runs widest-first; the output must not inherit that, or the list reorders itself
	// depending on which flags happen to be set.
	const vector<string> got = ComputeSetFlagNames(kTable, kCount, { 24 | 2, 0, 0 });
	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("NO ITEMS", got[0]);			// value 2, declared before the falling field
	EXPECT_EQ("FORCE FALLINGST", got[1]);
}

TEST(SetFlagNames, NothingSetNamesNothing)
{
	EXPECT_TRUE(ComputeSetFlagNames(kTable, kCount, { 0, 0, 0 }).empty());
}

TEST(SetFlagNames, IgnoresEntriesForWordsTheServerDidNotSend)
{
	// An older server sends fewer words than our table knows about. Those entries have nowhere to
	// match and must be skipped rather than read past the end of the vector.
	const vector<string> got = ComputeSetFlagNames(kTable, kCount, { 1 });
	ASSERT_EQ(1u, got.size());
	EXPECT_EQ("NO HEALTH", got[0]);
}

TEST(SetFlagNames, AServerThatSentNoWordsNamesNothing)
{
	// Must read as "unknown", never as "no flags set" -- which is why the caller checks the count
	// before deciding what to draw.
	EXPECT_TRUE(ComputeSetFlagNames(kTable, kCount, {}).empty());
}

TEST(SetFlagNames, ToleratesMoreWordsThanTheTableKnows)
{
	// The wire format is length-prefixed so a newer engine can add a seventh word; receiving one must
	// not disturb the six we can name.
	const vector<string> got = ComputeSetFlagNames(kTable, kCount, { 1, 0, 0, 0, 0, 0, 0xFFFF });
	ASSERT_EQ(1u, got.size());
	EXPECT_EQ("NO HEALTH", got[0]);
}

TEST(SetFlagNames, AnEmptyOrNullTableNamesNothing)
{
	EXPECT_TRUE(ComputeSetFlagNames(NULL, 5, { 1 }).empty());
	EXPECT_TRUE(ComputeSetFlagNames(kTable, 0, { 1 }).empty());
	EXPECT_TRUE(ComputeSetFlagNames(kTable, -1, { 1 }).empty());
}

TEST(SetFlagNames, SurvivesTheHighBitBeingSet)
{
	// The words arrive as signed longs off the wire; bit 31 must not turn into a negative-index or a
	// sign-extended comparison that matches everything.
	const FlagNameEntry high[] = { { 0, 0x80000000u, "HIGH BIT" } };
	const vector<string> got = ComputeSetFlagNames(high, 1, { static_cast<int>(0x80000000u) });
	ASSERT_EQ(1u, got.size());
	EXPECT_EQ("HIGH BIT", got[0]);
}
