// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/wad-library/computation/pathdisplay_compute.h"

using namespace zx;

namespace
{

// One unit per character, so a width in this test is a character count and the expectations read as
// the lines a reader would count off the string themselves.
int Monospace(const char *text, void *)
{
	int n = 0;
	while (text[n] != 0)
		++n;

	return n;
}

std::string Joined(const std::vector<std::string> &lines)
{
	std::string out;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		if (i != 0)
			out += "|";
		out += lines[i];
	}

	return out;
}

std::vector<std::string> Wrap(const std::string &path, int width)
{
	return ComputeWrappedPath(path, width, Monospace, NULL);
}

} // namespace

TEST(PathDisplayTest, APathThatFitsIsOneLine)
{
	const std::vector<std::string> lines = Wrap("/wads/av.wad", 40);
	ASSERT_EQ(1u, lines.size());
	EXPECT_EQ("/wads/av.wad", lines[0]);
}

// [rc4l] Nothing may be dropped: the whole path rejoined has to be the path that went in.
TEST(PathDisplayTest, WrappingLosesNothing)
{
	const std::string path = "/Users/talhataj/Documents/GZDoom/wads/doom/av.wad";
	const std::vector<std::string> lines = Wrap(path, 16);

	std::string rejoined;
	for (size_t i = 0; i < lines.size(); ++i)
		rejoined += lines[i];

	EXPECT_EQ(path, rejoined);
	EXPECT_GT(lines.size(), 1u);
}

TEST(PathDisplayTest, NoLineExceedsTheWidth)
{
	const std::vector<std::string> lines =
		Wrap("/Users/talhataj/Documents/GZDoom/wads/doom/av.wad", 16);

	for (size_t i = 0; i < lines.size(); ++i)
		EXPECT_LE(Monospace(lines[i].c_str(), NULL), 16) << "line " << i << ": " << lines[i];
}

// Every line after the first begins with a separator, so it reads as a continuation.
TEST(PathDisplayTest, BreaksFallBeforeASeparator)
{
	const std::vector<std::string> lines = Wrap("/aaaa/bbbb/cccc/dddd", 11);
	ASSERT_GT(lines.size(), 1u);

	for (size_t i = 0; i < lines.size(); ++i)
		EXPECT_EQ('/', lines[i][0]) << "line " << i << ": " << lines[i];

	EXPECT_EQ("/aaaa/bbbb|/cccc/dddd", Joined(lines));
}

// [rc4l] A filename can be longer than the box; splitting it beats overflowing silently.
TEST(PathDisplayTest, AComponentTooWideIsSplitByCharacter)
{
	const std::vector<std::string> lines = Wrap("/abcdefghijklmnop", 6);
	ASSERT_GT(lines.size(), 1u);

	std::string rejoined;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		EXPECT_LE(Monospace(lines[i].c_str(), NULL), 6);
		EXPECT_FALSE(lines[i].empty());
		rejoined += lines[i];
	}

	EXPECT_EQ("/abcdefghijklmnop", rejoined);
}

TEST(PathDisplayTest, WindowsSeparatorsBreakToo)
{
	const std::vector<std::string> lines = Wrap("C:\\games\\doom\\wads\\av.wad", 12);
	ASSERT_GT(lines.size(), 1u);

	std::string rejoined;
	for (size_t i = 0; i < lines.size(); ++i)
		rejoined += lines[i];

	EXPECT_EQ("C:\\games\\doom\\wads\\av.wad", rejoined);
}

TEST(PathDisplayTest, AnEmptyPathGivesNoLines)
{
	EXPECT_TRUE(Wrap("", 40).empty());
}

TEST(PathDisplayTest, NoMeasurerGivesNoLines)
{
	EXPECT_TRUE(ComputeWrappedPath("/wads/av.wad", 40, NULL, NULL).empty());
}

// A width nothing could fit would split forever, so the path comes back whole instead.
TEST(PathDisplayTest, AnImpossibleWidthDoesNotHang)
{
	const std::vector<std::string> lines = Wrap("/wads/av.wad", 0);
	ASSERT_EQ(1u, lines.size());
	EXPECT_EQ("/wads/av.wad", lines[0]);
}

TEST(PathDisplayTest, ARelativePathIsWrappedToo)
{
	const std::vector<std::string> lines = Wrap("wads/doom/av.wad", 10);
	ASSERT_GT(lines.size(), 1u);
	EXPECT_EQ("wads/doom|/av.wad", Joined(lines));
}

//*****************************************************************************
//
// The tip body.

TEST(PathDisplayTest, TheTipNamesTheFileThenSaysWhereItIs)
{
	const std::string tip = ComputePathTip("av.wad", PathTipState::Found, "/wads/av.wad", 40,
		Monospace, NULL);

	EXPECT_EQ("av.wad\n/wads/av.wad", tip);
}

TEST(PathDisplayTest, APendingResolveSaysSoRatherThanNothing)
{
	const std::string tip = ComputePathTip("av.wad", PathTipState::Pending, "", 40, Monospace, NULL);
	EXPECT_EQ("av.wad\nLooking...", tip);
}

TEST(PathDisplayTest, AMissingFileSaysWhyThereIsNoPath)
{
	const std::string tip = ComputePathTip("av.wad", PathTipState::Missing, "", 40, Monospace, NULL);
	EXPECT_EQ("av.wad\nNot on this machine", tip);
}

// [rc4l] Found with no path is a bug elsewhere; the tip must not quietly hide it by showing a name
// and nothing else.
TEST(PathDisplayTest, FoundWithNoPathIsTreatedAsMissing)
{
	const std::string tip = ComputePathTip("av.wad", PathTipState::Found, "", 40, Monospace, NULL);
	EXPECT_EQ("av.wad\nNot on this machine", tip);
}

TEST(PathDisplayTest, ALongPathArrivesAsSeveralLines)
{
	const std::string tip = ComputePathTip("av.wad", PathTipState::Found,
		"/Users/talhataj/Documents/GZDoom/wads/doom/av.wad", 16, Monospace, NULL);

	int newlines = 0;
	for (size_t i = 0; i < tip.size(); ++i)
	{
		if (tip[i] == '\n')
			++newlines;
	}

	EXPECT_GT(newlines, 1);
	EXPECT_EQ("av.wad", tip.substr(0, 6));
}
