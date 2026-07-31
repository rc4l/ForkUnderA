// [rc4l] Tests for deriving the version tag and release channel from `git describe`.
//
// The channel is user-facing and shown in the console and window title, so the direction of any
// error matters: labelling an experimental build "stable" would send someone chasing a bug in a
// release that never contained the code. Every ambiguous input therefore has to come out
// experimental, and that is what most of these cases pin.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"
#include "features/fua-branding/computation/fua_version_compute.h"

#include <cstring>

using namespace zx;

namespace
{
// [rc4l] Small helper so each case reads as one line.
const char *TagOf(const char *describe, size_t bufSize = 64)
{
	static char buf[128];
	memset(buf, 0xAA, sizeof buf);            // poison, so a missing NUL shows up as garbage
	FuaVersionTag(describe, buf, bufSize > sizeof buf ? sizeof buf : bufSize);
	return buf;
}
}

TEST(FuaVersion, ExactTagIsStable)
{
	// A build made at the tag itself: `git describe` emits just the tag.
	EXPECT_TRUE(FuaIsStableBuild("v0.1.19"));
	EXPECT_STREQ(TagOf("v0.1.19"), "v0.1.19");
}

TEST(FuaVersion, CommitsAfterATagAreExperimental)
{
	// The real shape from this tree: tag, commits-since, short hash.
	EXPECT_FALSE(FuaIsStableBuild("v0.1.19-29-gde55d35"));
	EXPECT_STREQ(TagOf("v0.1.19-29-gde55d35"), "v0.1.19");
}

TEST(FuaVersion, TagsContainingDashesSurvive)
{
	// Scanning from the right is what makes this work -- a left-to-right search would cut the tag
	// at its own dash and report "v1.0" for a v1.0-rc1 build.
	EXPECT_TRUE(FuaIsStableBuild("v1.0-rc1"));
	EXPECT_STREQ(TagOf("v1.0-rc1"), "v1.0-rc1");
	EXPECT_FALSE(FuaIsStableBuild("v1.0-rc1-3-gabc1234"));
	EXPECT_STREQ(TagOf("v1.0-rc1-3-gabc1234"), "v1.0-rc1");
}

TEST(FuaVersion, AmbiguousInputIsNeverCalledStable)
{
	// Empty or missing: a shallow clone with no tags describes as nothing at all. Claiming
	// "stable" here would be the harmful direction to be wrong in.
	EXPECT_FALSE(FuaIsStableBuild(""));
	EXPECT_FALSE(FuaIsStableBuild(nullptr));
}

TEST(FuaVersion, NoTagAtAllStillShowsSomething)
{
	// A bare hash (no tag reachable) must not blank the line -- show what we have.
	EXPECT_STREQ(TagOf("de55d35"), "de55d35");
	EXPECT_FALSE(FuaIsStableBuild("de55d35"));
}

TEST(FuaVersion, SuffixLookalikesAreNotTreatedAsSuffixes)
{
	// "-g..." without a preceding "-<digits>" is part of the tag, not a describe suffix.
	EXPECT_TRUE(FuaIsStableBuild("v2.0-gold"));
	EXPECT_STREQ(TagOf("v2.0-gold"), "v2.0-gold");
	// A trailing "-g" with nothing after it is not a hash either.
	EXPECT_TRUE(FuaIsStableBuild("v2.0-1-g"));
}

TEST(FuaVersion, DashGNotPrecededByACountIsPartOfTheTag)
{
	// git only emits "-<count>-g<hash>", so a "-g" with no digit run before it belongs to the tag
	// itself. Both shapes must be ignored: one where the preceding char is a letter, and one where
	// the "-g" is at the very start with nothing before it at all.
	EXPECT_STREQ(TagOf("v1.0-beta-gabc123"), "v1.0-beta-gabc123");
	EXPECT_TRUE(FuaIsStableBuild("v1.0-beta-gabc123"));

	EXPECT_STREQ(TagOf("-gabc123"), "-gabc123");
	EXPECT_FALSE(FuaIsStableBuild("-gabc123"));   // does not look like a version tag
}

TEST(FuaVersion, OutputIsAlwaysTerminatedAndNeverOverruns)
{
	char small[5];
	memset(small, 0xAA, sizeof small);
	FuaVersionTag("v0.1.19-29-gde55d35", small, sizeof small);
	EXPECT_EQ(strlen(small), 4u);          // 4 chars + NUL
	EXPECT_STREQ(small, "v0.1");
	EXPECT_EQ(small[4], '\0');

	// Degenerate sizes must not write past the end.
	char one[1] = { (char)0xAA };
	FuaVersionTag("v0.1.19", one, 1);
	EXPECT_EQ(one[0], '\0');

	// Zero size and null buffer are no-ops rather than crashes.
	FuaVersionTag("v0.1.19", nullptr, 0);
	char guard = (char)0xAA;
	FuaVersionTag("v0.1.19", &guard, 0);
	EXPECT_EQ(guard, (char)0xAA);
}

TEST(FuaVersion, NullDescribeYieldsEmptyString)
{
	char buf[16];
	memset(buf, 0xAA, sizeof buf);
	FuaVersionTag(nullptr, buf, sizeof buf);
	EXPECT_STREQ(buf, "");
}
