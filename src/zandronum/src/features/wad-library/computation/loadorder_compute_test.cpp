// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/wad-library/computation/loadorder_compute.h"

using namespace zx;

namespace
{

LoadOrderEntry Entry(const char *path, const char *name, long long size = 1,
	const char *md5 = "")
{
	LoadOrderEntry e(path, name, size);
	e.md5 = md5;
	return e;
}

} // namespace

// ---------------------------------------------------------------- adding

TEST(LoadOrder, AddsToTheEndBecauseLaterMeansLoadedLater)
{
	std::vector<LoadOrderEntry> list;

	EXPECT_EQ(AddVerdict::Added, AddToLoadOrder(list, Entry("a/one.wad", "one.wad")).verdict);
	EXPECT_EQ(AddVerdict::Added, AddToLoadOrder(list, Entry("a/two.wad", "two.wad")).verdict);

	ASSERT_EQ(2u, list.size());
	EXPECT_EQ("one.wad", list[0].name);
	EXPECT_EQ("two.wad", list[1].name);
}

TEST(LoadOrder, RefusesTheSameFileTwice)
{
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("a/one.wad", "one.wad"));

	const AddResult again = AddToLoadOrder(list, Entry("a/one.wad", "one.wad"));

	EXPECT_EQ(AddVerdict::AlreadyThere, again.verdict);
	EXPECT_EQ(0u, again.index);
	EXPECT_EQ(1u, list.size());
}

TEST(LoadOrder, RefusesADifferentFileOfTheSameName)
{
	// The one that has to be EXPLAINED rather than silently ignored: two rows the player can see
	// are different, and the client can only be told one name.
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("a/brutal.pk3", "brutal.pk3", 4096));

	const AddResult clash = AddToLoadOrder(list, Entry("b/brutal.pk3", "brutal.pk3", 8192));

	EXPECT_EQ(AddVerdict::NameTaken, clash.verdict);
	EXPECT_EQ(0u, clash.index) << "points at the one already holding the name, so it can be shown";
	EXPECT_EQ(1u, list.size());
}

TEST(LoadOrder, TheSameContentReachedByTwoPathsIsStillTheSameFile)
{
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("a/x.wad", "x.wad", 10, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));

	const AddResult again =
		AddToLoadOrder(list, Entry("b/x.wad", "x.wad", 10, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));

	EXPECT_EQ(AddVerdict::AlreadyThere, again.verdict);
}

TEST(LoadOrder, TheNameTestIgnoresCase)
{
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("a/Map01.WAD", "Map01.WAD"));

	EXPECT_EQ(AddVerdict::NameTaken,
		AddToLoadOrder(list, Entry("b/map01.wad", "map01.wad")).verdict);
}

TEST(LoadOrder, RefusesAnEntryWithNothingInIt)
{
	std::vector<LoadOrderEntry> list;

	EXPECT_EQ(AddVerdict::Empty, AddToLoadOrder(list, Entry("", "x.wad")).verdict);
	EXPECT_EQ(AddVerdict::Empty, AddToLoadOrder(list, Entry("a/x.wad", "")).verdict);
	EXPECT_TRUE(list.empty());
}

TEST(LoadOrder, AnUnhashedEntryFallsToTheNameAnswerRatherThanMatchingOnEmpty)
{
	// Both md5s empty must NOT read as "the same content", or every unhashed file would collide
	// with every other unhashed file of that name and report the wrong reason.
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("a/x.wad", "x.wad", 10, ""));

	EXPECT_EQ(AddVerdict::NameTaken, AddToLoadOrder(list, Entry("b/x.wad", "x.wad", 20, "")).verdict);
}

// ---------------------------------------------------------------- moving

TEST(LoadOrder, MovesOnePlaceAtATime)
{
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("a/1.wad", "1.wad"));
	AddToLoadOrder(list, Entry("a/2.wad", "2.wad"));
	AddToLoadOrder(list, Entry("a/3.wad", "3.wad"));

	EXPECT_EQ(0u, MoveInLoadOrder(list, 1, -1));
	EXPECT_EQ("2.wad", list[0].name);
	EXPECT_EQ("1.wad", list[1].name);

	EXPECT_EQ(1u, MoveInLoadOrder(list, 0, +1));
	EXPECT_EQ("1.wad", list[0].name);
	EXPECT_EQ("2.wad", list[1].name);
}

TEST(LoadOrder, StopsAtBothEndsRatherThanWrapping)
{
	// Wrapping would move a file from the end of the load order to the front on one keypress too
	// many, which is a server loading in an order nobody chose.
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("a/1.wad", "1.wad"));
	AddToLoadOrder(list, Entry("a/2.wad", "2.wad"));

	EXPECT_EQ(0u, MoveInLoadOrder(list, 0, -1));
	EXPECT_EQ("1.wad", list[0].name);

	EXPECT_EQ(1u, MoveInLoadOrder(list, 1, +1));
	EXPECT_EQ("2.wad", list[1].name);
}

TEST(LoadOrder, MovingNothingOrAnythingOutOfRangeIsSafe)
{
	std::vector<LoadOrderEntry> list;
	EXPECT_EQ(9u, MoveInLoadOrder(list, 9, -1));

	AddToLoadOrder(list, Entry("a/1.wad", "1.wad"));
	EXPECT_EQ(0u, MoveInLoadOrder(list, 0, 0));
	EXPECT_EQ(1u, list.size());
}

// ---------------------------------------------------------------- removing

TEST(LoadOrder, RemovingLeavesTheSelectionWhereTheEyeAlreadyIs)
{
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("a/1.wad", "1.wad"));
	AddToLoadOrder(list, Entry("a/2.wad", "2.wad"));
	AddToLoadOrder(list, Entry("a/3.wad", "3.wad"));

	// The row that slid up into the gap.
	EXPECT_EQ(1u, RemoveFromLoadOrder(list, 1));
	ASSERT_EQ(2u, list.size());
	EXPECT_EQ("3.wad", list[1].name);
}

TEST(LoadOrder, RemovingTheLastRowFallsBackOntoTheNewLast)
{
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("a/1.wad", "1.wad"));
	AddToLoadOrder(list, Entry("a/2.wad", "2.wad"));

	EXPECT_EQ(0u, RemoveFromLoadOrder(list, 1));
	ASSERT_EQ(1u, list.size());
	EXPECT_EQ("1.wad", list[0].name);
}

TEST(LoadOrder, RemovingTheOnlyRowLeavesNothingSelected)
{
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("a/1.wad", "1.wad"));

	EXPECT_EQ(0u, RemoveFromLoadOrder(list, 0));
	EXPECT_TRUE(list.empty());
}

TEST(LoadOrder, RemovingOutOfRangeChangesNothing)
{
	std::vector<LoadOrderEntry> list;
	EXPECT_EQ(0u, RemoveFromLoadOrder(list, 3));

	AddToLoadOrder(list, Entry("a/1.wad", "1.wad"));
	EXPECT_EQ(0u, RemoveFromLoadOrder(list, 9));
	EXPECT_EQ(1u, list.size());
}

// ---------------------------------------------------------------- what the server is told

TEST(LoadOrder, TheIwadIsAlwaysFirst)
{
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("w/one.wad", "one.wad"));
	AddToLoadOrder(list, Entry("w/two.wad", "two.wad"));

	const std::vector<std::string> paths = LoadOrderPaths("i/doom2.wad", list);

	ASSERT_EQ(3u, paths.size());
	EXPECT_EQ("i/doom2.wad", paths[0]);
	EXPECT_EQ("w/one.wad", paths[1]);
	EXPECT_EQ("w/two.wad", paths[2]);
}

TEST(LoadOrder, ReorderingTheListCannotDisplaceTheIwad)
{
	// It is not IN the list, which is the point: there is no sequence of moves that puts anything
	// before it.
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("w/one.wad", "one.wad"));
	AddToLoadOrder(list, Entry("w/two.wad", "two.wad"));
	MoveInLoadOrder(list, 1, -1);

	const std::vector<std::string> paths = LoadOrderPaths("i/doom2.wad", list);
	EXPECT_EQ("i/doom2.wad", paths[0]);
}

TEST(LoadOrder, WithNoIwadChosenOnlyTheFilesAreNamed)
{
	std::vector<LoadOrderEntry> list;
	AddToLoadOrder(list, Entry("w/one.wad", "one.wad"));

	const std::vector<std::string> paths = LoadOrderPaths("", list);

	ASSERT_EQ(1u, paths.size());
	EXPECT_EQ("w/one.wad", paths[0]);
}

TEST(LoadOrder, AnEmptyEverythingIsAnEmptyCommandLine)
{
	const std::vector<LoadOrderEntry> list;
	EXPECT_TRUE(LoadOrderPaths("", list).empty());
}

TEST(AddToLoadOrder, TwoNamesOfDifferentLengthAreNotTheSameFile)
{
	// The cheap half of the name compare: lengths first, so "av.wad" and "av20.wad" are settled
	// without walking either of them.
	std::vector<LoadOrderEntry> list;
	list.push_back(LoadOrderEntry("/a/av.wad", "av.wad", 10));

	const AddResult r = AddToLoadOrder(list, LoadOrderEntry("/a/av20.wad", "av20.wad", 20));

	EXPECT_EQ(AddVerdict::Added, r.verdict);
	EXPECT_EQ(2u, list.size());
}

// [rc4l] The empty shapes. Both defaults are load-bearing: a LoadOrderEntry with no size yet is one
// that has not been measured, and an AddResult that nobody filled in reads as "nothing to add"
// rather than as a successful add of row zero.
TEST(LoadOrderShapes, StartAtNothingRatherThanAtWhateverWasOnTheStack)
{
	const LoadOrderEntry entry;
	EXPECT_TRUE(entry.path.empty());
	EXPECT_TRUE(entry.name.empty());
	EXPECT_TRUE(entry.md5.empty());
	EXPECT_EQ(0, entry.size);

	const AddResult result;
	EXPECT_EQ(AddVerdict::Empty, result.verdict);
	EXPECT_EQ(0u, result.index);
}
