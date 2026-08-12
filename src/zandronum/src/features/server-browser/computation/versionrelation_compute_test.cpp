// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/versionrelation_compute.h"

#include <gtest/gtest.h>

using zx::CompareFuaVersions;
using zx::VersionRelation;
using zx::VersionRelationCanJoin;
using zx::VersionRelationSinks;

namespace
{
const char *const kOurs = "v0.2.19";
}

// ---------------------------------------------------------------- direction

TEST(VersionRelation, TheSameVersionIsJoinable)
{
	EXPECT_EQ(VersionRelation::Same, CompareFuaVersions("v0.2.19", kOurs));
}

TEST(VersionRelation, AnOlderPatchIsOlder)
{
	EXPECT_EQ(VersionRelation::Older, CompareFuaVersions("v0.2.18", kOurs));
}

TEST(VersionRelation, ANewerPatchIsNewer)
{
	EXPECT_EQ(VersionRelation::Newer, CompareFuaVersions("v0.2.20", kOurs));
}

TEST(VersionRelation, TheMinorOutranksThePatch)
{
	// 0.3.0 beats 0.2.99: components are compared left to right, not lexically and not by the last
	// number, which is how "0.2.9 vs 0.2.10" goes wrong when compared as text.
	EXPECT_EQ(VersionRelation::Newer, CompareFuaVersions("v0.3.0", kOurs));
	EXPECT_EQ(VersionRelation::Older, CompareFuaVersions("v0.1.99", kOurs));
}

TEST(VersionRelation, TenIsAboveNineRatherThanBelowIt)
{
	// The comparison every string-based version check gets wrong.
	EXPECT_EQ(VersionRelation::Newer, CompareFuaVersions("v0.2.100", kOurs));
	EXPECT_EQ(VersionRelation::Newer, CompareFuaVersions("v0.10.0", "v0.9.0"));
}

// ---------------------------------------------------------------- shapes it must accept

TEST(VersionRelation, TheLeadingVIsOptional)
{
	EXPECT_EQ(VersionRelation::Same, CompareFuaVersions("0.2.19", kOurs));
	EXPECT_EQ(VersionRelation::Same, CompareFuaVersions("V0.2.19", kOurs));
}

TEST(VersionRelation, MissingComponentsAreZero)
{
	EXPECT_EQ(VersionRelation::Same, CompareFuaVersions("v0.2.0", "v0.2"));
	EXPECT_EQ(VersionRelation::Newer, CompareFuaVersions("v0.2.1", "v0.2"));
}

TEST(VersionRelation, TrailingTextIsIgnored)
{
	// What a server actually sends carries more than the tag.
	EXPECT_EQ(VersionRelation::Same, CompareFuaVersions("v0.2.19 on windows", kOurs));
	EXPECT_EQ(VersionRelation::Older, CompareFuaVersions("v0.2.18 on linux", kOurs));
}

// ---------------------------------------------------------------- experimental builds

TEST(VersionRelation, CommitsPastATagAreNewerThanTheTag)
{
	// git describe: "v0.2.19-29-gde55d35" is 29 commits after the release, so it is ahead of it.
	EXPECT_EQ(VersionRelation::Newer, CompareFuaVersions("v0.2.19-29-gde55d35", kOurs));
	EXPECT_EQ(VersionRelation::Older, CompareFuaVersions(kOurs, "v0.2.19-29-gde55d35"));
}

TEST(VersionRelation, TwoBuildsPastTheSameTagAreNotDistinguishable)
{
	// The describe string does not say which came first, and inventing an order would be a guess the
	// player pays for. Same is what the join check has always assumed for these.
	EXPECT_EQ(VersionRelation::Same,
		CompareFuaVersions("v0.2.19-29-gde55d35", "v0.2.19-4-gaaaaaaa"));
}

TEST(VersionRelation, ANewerTagBeatsAnExperimentalOlderOne)
{
	// The suffix only decides when the numbers are equal; it is not a bonus.
	EXPECT_EQ(VersionRelation::Older, CompareFuaVersions("v0.2.18-90-gde55d35", kOurs));
}

// ---------------------------------------------------------------- refusals

TEST(VersionRelation, AVersionWeCannotReadIsUnknownRatherThanOld)
{
	// Guessing "behind" would sink a row for a reason nobody could verify.
	EXPECT_EQ(VersionRelation::Unknown, CompareFuaVersions("", kOurs));
	EXPECT_EQ(VersionRelation::Unknown, CompareFuaVersions("not a version", kOurs));
	EXPECT_EQ(VersionRelation::Unknown, CompareFuaVersions("v", kOurs));
	EXPECT_EQ(VersionRelation::Unknown, CompareFuaVersions("vv0.2.19", kOurs));
}

TEST(VersionRelation, AnUnreadableOwnVersionIsAlsoUnknown)
{
	// Symmetric on purpose: if we cannot read OUR version, we cannot claim anything about theirs.
	EXPECT_EQ(VersionRelation::Unknown, CompareFuaVersions("v0.2.19", ""));
}

TEST(VersionRelation, AnAbsurdlyLongNumberDoesNotWrapIntoLookingOlder)
{
	// A component big enough to overflow would compare SMALLER than ours if it wrapped, turning a
	// hostile string into a server that merely looks out of date. It saturates instead.
	EXPECT_EQ(VersionRelation::Newer, CompareFuaVersions("v0.2.999999999999999999999", kOurs));
}

TEST(VersionRelation, LeadingWhitespaceIsTolerated)
{
	EXPECT_EQ(VersionRelation::Same, CompareFuaVersions("  v0.2.19", kOurs));
}

// ---------------------------------------------------------------- what the browser does with it

TEST(VersionRelation, OnlyTheSameVersionCanBeJoined)
{
	EXPECT_TRUE(VersionRelationCanJoin(VersionRelation::Same));
	EXPECT_FALSE(VersionRelationCanJoin(VersionRelation::Older));
	EXPECT_FALSE(VersionRelationCanJoin(VersionRelation::Newer));
	EXPECT_FALSE(VersionRelationCanJoin(VersionRelation::Unknown));
}

TEST(VersionRelation, OnlyWhatThePlayerCannotFixSinks)
{
	// Newer stays in place because updating reaches it; the other two do not move for anyone.
	EXPECT_TRUE(VersionRelationSinks(VersionRelation::Older));
	EXPECT_TRUE(VersionRelationSinks(VersionRelation::Unknown));
	EXPECT_FALSE(VersionRelationSinks(VersionRelation::Newer));
	EXPECT_FALSE(VersionRelationSinks(VersionRelation::Same));
}
