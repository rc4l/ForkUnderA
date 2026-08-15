// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <algorithm>

#include "features/wad-library/computation/wadlibrary_compute.h"

using namespace zx;

namespace
{

LibraryFile File(const char *path, const char *name, long long size, const char *folder = "wads")
{
	LibraryFile f;
	f.path = path;
	f.name = name;
	f.folder = folder;
	f.size = size;
	f.key = SearchFold(name);
	return f;
}

} // namespace

// ---------------------------------------------------------------- which files we offer

TEST(WadLibrary, TakesTheExtensionsTheEngineCanBeHandled)
{
	EXPECT_TRUE(IsLoadableWadName("sunder.wad"));
	EXPECT_TRUE(IsLoadableWadName("brutal.pk3"));
	EXPECT_TRUE(IsLoadableWadName("thing.PK7"));
	EXPECT_TRUE(IsLoadableWadName("patch.deh"));
	EXPECT_TRUE(IsLoadableWadName("patch.BEX"));
}

TEST(WadLibrary, RefusesWhatIsNotLoadable)
{
	EXPECT_FALSE(IsLoadableWadName("readme.txt"));
	EXPECT_FALSE(IsLoadableWadName("screenshot.png"));
	EXPECT_FALSE(IsLoadableWadName("noextension"));
	EXPECT_FALSE(IsLoadableWadName(""));
	EXPECT_FALSE(IsLoadableWadName("trailingdot."));
}

TEST(WadLibrary, HidesTheEnginesOwnFiles)
{
	// Loading one of these again is loading it twice.
	EXPECT_TRUE(IsEngineOwnedName("fua_core_v0.2.21.pk3"));
	EXPECT_TRUE(IsEngineOwnedName("brightmaps.pk3"));
	EXPECT_TRUE(IsEngineOwnedName("skulltag_actors.pk3"));

	// The version moves, so the test has to be a prefix or it stops working on the next release.
	EXPECT_TRUE(IsEngineOwnedName("fua_core_v9.9.99.pk3"));

	EXPECT_FALSE(IsEngineOwnedName("brutalv21.pk3"));
}

TEST(WadLibrary, HidesIwadsBecauseTheyAreTheOtherList)
{
	EXPECT_TRUE(IsKnownIwadName("doom2.wad"));
	EXPECT_TRUE(IsKnownIwadName("DOOM2.WAD"));
	EXPECT_TRUE(IsKnownIwadName("freedoom2.wad"));

	// A prefix test would swallow this one, which is an ordinary pwad.
	EXPECT_FALSE(IsKnownIwadName("doom2_extras.wad"));
	EXPECT_FALSE(IsKnownIwadName("plutonia2.wad"));
}

TEST(KnownIwadNames, IsTheSameTableIsKnownIwadNameTests)
{
	// The two are used from opposite directions -- this list feeds the IWAD picker, the predicate
	// hides those files from the wad list -- so a name in one and not the other is a file offered
	// twice or offered nowhere.
	const std::vector<std::string> &names = KnownIwadNames();

	ASSERT_FALSE(names.empty());
	for (size_t i = 0; i < names.size(); ++i)
		EXPECT_TRUE(IsKnownIwadName(names[i])) << names[i];
}

TEST(KnownIwadNames, CarriesTheOnesPeopleActuallyOwn)
{
	const std::vector<std::string> &names = KnownIwadNames();

	bool haveDoom2 = false;
	bool haveFreedoom2 = false;
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (names[i] == "doom2.wad") haveDoom2 = true;
		if (names[i] == "freedoom2.wad") haveFreedoom2 = true;
	}

	// [rc4l] doom2.wad above all. The IWAD picker was built on a list that only knew the free
	// stand-ins, so it offered Freedoom to somebody running Doom II and hosted a server their own
	// client could not authenticate against.
	EXPECT_TRUE(haveDoom2);
	EXPECT_TRUE(haveFreedoom2);
}

// ---------------------------------------------------------------- search

TEST(SearchFold, DropsEverythingPeopleSpellInconsistently)
{
	EXPECT_EQ("brutalv21", SearchFold("Brutal_v21.pk3").substr(0, 9));
	EXPECT_EQ(SearchFold("brutal-v21"), SearchFold("Brutal_V21"));
	EXPECT_EQ(SearchFold("brutal v21"), SearchFold("BrutalV21"));
}

TEST(SearchFold, IsEmptyForTextWithNothingToMatchOn)
{
	EXPECT_EQ("", SearchFold(""));
	EXPECT_EQ("", SearchFold("___---..."));
}

TEST(LibraryMatches, AnEmptySearchIsEveryFile)
{
	EXPECT_TRUE(LibraryMatches(File("a/x.wad", "x.wad", 1), ""));
}

TEST(LibraryMatches, FindsAMiddleOfTheNameNotJustTheStart)
{
	// Real names carry author prefixes and version suffixes, so prefix matching would fail the
	// search people actually type.
	const LibraryFile f = File("a/3-sunder-2407.wad", "3-sunder-2407.wad", 1);
	EXPECT_TRUE(LibraryMatches(f, SearchFold("sunder")));
	EXPECT_TRUE(LibraryMatches(f, SearchFold("2407")));
	EXPECT_FALSE(LibraryMatches(f, SearchFold("valiant")));
}

// ---------------------------------------------------------------- rows

TEST(BuildLibraryRows, OneRowPerFileWhenNothingRepeats)
{
	std::vector<LibraryFile> files;
	files.push_back(File("a/alpha.wad", "alpha.wad", 10));
	files.push_back(File("a/beta.wad", "beta.wad", 20));

	const std::vector<LibraryRow> rows = BuildLibraryRows(files, "");

	ASSERT_EQ(2u, rows.size());
	EXPECT_EQ(1, rows[0].copies);
	EXPECT_EQ("alpha.wad", files[rows[0].index].name);
	EXPECT_EQ("beta.wad", files[rows[1].index].name);
}

TEST(BuildLibraryRows, TheSameFileInTwoFoldersIsOneRow)
{
	// Same name and same size: as close to "the same file" as we can get without reading either of
	// them, which is the whole trade this unit is built on.
	std::vector<LibraryFile> files;
	files.push_back(File("a/map01.wad", "map01.wad", 4096, "a"));
	files.push_back(File("b/map01.wad", "map01.wad", 4096, "b"));

	const std::vector<LibraryRow> rows = BuildLibraryRows(files, "");

	ASSERT_EQ(1u, rows.size());
	EXPECT_EQ(2, rows[0].copies);
}

TEST(BuildLibraryRows, TwoBuildsOfOneModStayTwoRows)
{
	// The case the player MUST be able to see: one name, two different files.
	std::vector<LibraryFile> files;
	files.push_back(File("a/brutal.pk3", "brutal.pk3", 4096));
	files.push_back(File("b/brutal.pk3", "brutal.pk3", 8192));

	const std::vector<LibraryRow> rows = BuildLibraryRows(files, "");

	ASSERT_EQ(2u, rows.size());
	EXPECT_EQ(1, rows[0].copies);
	EXPECT_EQ(1, rows[1].copies);
	EXPECT_NE(files[rows[0].index].size, files[rows[1].index].size);
}

TEST(BuildLibraryRows, ThreeCopiesCountAsThree)
{
	std::vector<LibraryFile> files;
	files.push_back(File("a/x.wad", "x.wad", 7, "a"));
	files.push_back(File("b/x.wad", "x.wad", 7, "b"));
	files.push_back(File("c/x.wad", "x.wad", 7, "c"));

	const std::vector<LibraryRow> rows = BuildLibraryRows(files, "");

	ASSERT_EQ(1u, rows.size());
	EXPECT_EQ(3, rows[0].copies);
}

TEST(BuildLibraryRows, SortsByNameWhateverOrderTheScanFoundThemIn)
{
	std::vector<LibraryFile> files;
	files.push_back(File("a/zulu.wad", "zulu.wad", 1));
	files.push_back(File("a/alpha.wad", "alpha.wad", 1));
	files.push_back(File("a/mike.wad", "mike.wad", 1));

	const std::vector<LibraryRow> rows = BuildLibraryRows(files, "");

	ASSERT_EQ(3u, rows.size());
	EXPECT_EQ("alpha.wad", files[rows[0].index].name);
	EXPECT_EQ("mike.wad", files[rows[1].index].name);
	EXPECT_EQ("zulu.wad", files[rows[2].index].name);
}

TEST(BuildLibraryRows, TheSearchFiltersAndTheRestStaysSorted)
{
	std::vector<LibraryFile> files;
	files.push_back(File("a/sunder.wad", "sunder.wad", 1));
	files.push_back(File("a/valiant.wad", "valiant.wad", 1));
	files.push_back(File("a/sunlust.wad", "sunlust.wad", 1));

	const std::vector<LibraryRow> rows = BuildLibraryRows(files, SearchFold("sun"));

	ASSERT_EQ(2u, rows.size());
	EXPECT_EQ("sunder.wad", files[rows[0].index].name);
	EXPECT_EQ("sunlust.wad", files[rows[1].index].name);
}

TEST(BuildLibraryRows, AnEmptySetIsAnEmptyList)
{
	const std::vector<LibraryFile> files;
	EXPECT_TRUE(BuildLibraryRows(files, "").empty());
	EXPECT_TRUE(BuildLibraryRows(files, SearchFold("anything")).empty());
}

TEST(BuildLibraryRows, ASearchThatMatchesNothingIsEmptyRatherThanEverything)
{
	std::vector<LibraryFile> files;
	files.push_back(File("a/alpha.wad", "alpha.wad", 1));

	EXPECT_TRUE(BuildLibraryRows(files, SearchFold("zzz")).empty());
}

TEST(BuildLibraryRows, DeduplicationSurvivesTheFilter)
{
	// The merge walks the SORTED rows, so it has to keep working once a filter has removed some.
	std::vector<LibraryFile> files;
	files.push_back(File("a/sunder.wad", "sunder.wad", 99, "a"));
	files.push_back(File("a/valiant.wad", "valiant.wad", 1));
	files.push_back(File("b/sunder.wad", "sunder.wad", 99, "b"));

	const std::vector<LibraryRow> rows = BuildLibraryRows(files, SearchFold("sunder"));

	ASSERT_EQ(1u, rows.size());
	EXPECT_EQ(2, rows[0].copies);
}

TEST(BuildLibraryRows, OrderDoesNotDependOnScanOrder)
{
	// Two files of one name and one size, found either way round, must produce the same winner --
	// otherwise the row a player clicks changes between runs.
	std::vector<LibraryFile> forward;
	forward.push_back(File("a/x.wad", "x.wad", 5, "a"));
	forward.push_back(File("b/x.wad", "x.wad", 5, "b"));

	std::vector<LibraryFile> backward;
	backward.push_back(File("b/x.wad", "x.wad", 5, "b"));
	backward.push_back(File("a/x.wad", "x.wad", 5, "a"));

	const std::vector<LibraryRow> a = BuildLibraryRows(forward, "");
	const std::vector<LibraryRow> b = BuildLibraryRows(backward, "");

	ASSERT_EQ(1u, a.size());
	ASSERT_EQ(1u, b.size());
	EXPECT_EQ(forward[a[0].index].path, backward[b[0].index].path);
}

// ---------------------------------------------------------------- the shared order

TEST(LibraryFileLess, OrdersByFoldedNameThenSizeThenPath)
{
	EXPECT_TRUE(LibraryFileLess(File("a/alpha.wad", "alpha.wad", 1), File("a/beta.wad", "beta.wad", 1)));
	EXPECT_TRUE(LibraryFileLess(File("a/x.wad", "x.wad", 1), File("a/x.wad", "x.wad", 2)));
	EXPECT_TRUE(LibraryFileLess(File("a/x.wad", "x.wad", 1), File("b/x.wad", "x.wad", 1)));
	EXPECT_FALSE(LibraryFileLess(File("a/x.wad", "x.wad", 1), File("a/x.wad", "x.wad", 1)));
}

TEST(BuildLibraryRows, AlreadySortedInputComesOutInTheSameOrder)
{
	// This is what lets the caller sort once on its worker and skip the sort here: the two orders
	// are one function, so a presorted input must survive untouched.
	std::vector<LibraryFile> files;
	files.push_back(File("a/alpha.wad", "alpha.wad", 1));
	files.push_back(File("a/mike.wad", "mike.wad", 1));
	files.push_back(File("a/zulu.wad", "zulu.wad", 1));
	ASSERT_TRUE(std::is_sorted(files.begin(), files.end(), LibraryFileLess));

	const std::vector<LibraryRow> rows = BuildLibraryRows(files, "");

	ASSERT_EQ(3u, rows.size());
	EXPECT_EQ(0u, rows[0].index);
	EXPECT_EQ(1u, rows[1].index);
	EXPECT_EQ(2u, rows[2].index);
}

// ---------------------------------------------------------------- caps

TEST(LibraryCaps, AreWellAboveTheCollectionThisWasDesignedFor)
{
	EXPECT_GT(LibraryFileCap(), static_cast<size_t>(20000));
	EXPECT_GE(LibraryDepthCap(), 3);
}

TEST(LibraryRow, DefaultsToOneCopyRatherThanNone)
{
	// One is the honest default: a row exists because a file was found, so the count can never
	// start at zero without the row claiming a file nobody has.
	const LibraryRow row;

	EXPECT_EQ(0u, row.index);
	EXPECT_EQ(1, row.copies);
}
