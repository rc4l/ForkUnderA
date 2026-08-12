// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/addon-catalogue/computation/maprotation_compute.h"

using zx::MapsInRotation;

TEST(MapRotation, ACfgWithNoRotationOffersNoMaps)
{
	// True of several packs: Doom Barracks Zone leans on its own mapinfo chain and writes none.
	EXPECT_TRUE(MapsInRotation("cooperative true\nskill 3\n").empty());
	EXPECT_TRUE(MapsInRotation("").empty());
}

TEST(MapRotation, TheMapsComeOutInTheOrderWritten)
{
	// The order is the whole point. It is what "start on the third one" means.
	const std::vector<std::string> maps = MapsInRotation(
		"sv_randommaprotation 0\n"
		"addmap MAP01\n"
		"addmap MAP02\n"
		"addmap MAP03\n");

	ASSERT_EQ(3u, maps.size());
	EXPECT_EQ("MAP01", maps[0]);
	EXPECT_EQ("MAP02", maps[1]);
	EXPECT_EQ("MAP03", maps[2]);
}

TEST(MapRotation, TheCommandIsReadEitherWay)
{
	// [rc4l] Ghouls vs Humans writes `addmap Gvh00` and everything else writes lower case. A reader
	// that cared would silently return nothing for one of them.
	const std::vector<std::string> maps = MapsInRotation("AddMap Gvh00\nADDMAP GVHPDX01\naddmap MAP01\n");

	ASSERT_EQ(3u, maps.size());
	EXPECT_EQ("Gvh00", maps[0]);
	EXPECT_EQ("GVHPDX01", maps[1]);
}

TEST(MapRotation, TheMapNameKeepsItsOwnCase)
{
	// It is a lump name and goes back out as one. Lowering it would be a guess about the wad.
	const std::vector<std::string> maps = MapsInRotation("addmap Z1INV01\n");

	ASSERT_EQ(1u, maps.size());
	EXPECT_EQ("Z1INV01", maps[0]);
}

TEST(MapRotation, LeadingWhitespaceAndTabsAreFine)
{
	const std::vector<std::string> maps = MapsInRotation("  addmap MAP01\n\t\taddmap\tMAP02\n");

	ASSERT_EQ(2u, maps.size());
	EXPECT_EQ("MAP01", maps[0]);
	EXPECT_EQ("MAP02", maps[1]);
}

TEST(MapRotation, ACommentedLineIsNotARotation)
{
	// [rc4l] The cfgs in this catalogue are heavily commented and several of those comments talk
	// about the maps they are not adding.
	const std::vector<std::string> maps = MapsInRotation(
		"// addmap MAP31\n"
		"  // addmap MAP32\n"
		"addmap MAP01\n");

	ASSERT_EQ(1u, maps.size());
	EXPECT_EQ("MAP01", maps[0]);
}

TEST(MapRotation, ATrailingCommentDoesNotBecomePartOfTheName)
{
	const std::vector<std::string> maps = MapsInRotation("addmap MAP01 // the first one\n");

	ASSERT_EQ(1u, maps.size());
	EXPECT_EQ("MAP01", maps[0]);
}

TEST(MapRotation, ACommandThatMerelyStartsWithAddmapIsNotOne)
{
	// Without the check that the word ENDS, `addmapcycle` would read as `addmap cycle` and put a
	// map called "cycle" in the picker.
	EXPECT_TRUE(MapsInRotation("addmapcycle MAP01\n").empty());
	EXPECT_TRUE(MapsInRotation("readdmap MAP01\n").empty());
}

TEST(MapRotation, AddmapWithNothingAfterItAddsNothing)
{
	EXPECT_TRUE(MapsInRotation("addmap\n").empty());
	EXPECT_TRUE(MapsInRotation("addmap   \n").empty());
}

TEST(MapRotation, AMapNamedTwiceIsOneStop)
{
	// A rotation may visit a map twice; it is still one place to start from, and two stops that do
	// the same thing is a picker that looks broken.
	const std::vector<std::string> maps = MapsInRotation("addmap MAP01\naddmap MAP02\naddmap MAP01\n");

	ASSERT_EQ(2u, maps.size());
	EXPECT_EQ("MAP01", maps[0]);
	EXPECT_EQ("MAP02", maps[1]);
}

TEST(MapRotation, TheLastLineCountsWithoutATrailingNewline)
{
	const std::vector<std::string> maps = MapsInRotation("addmap MAP01\naddmap MAP02");

	ASSERT_EQ(2u, maps.size());
	EXPECT_EQ("MAP02", maps[1]);
}

TEST(MapRotation, CarriageReturnsAreNotPartOfTheName)
{
	// The cfgs ship with LF, and a player's own copy may not.
	const std::vector<std::string> maps = MapsInRotation("addmap MAP01\r\naddmap MAP02\r\n");

	ASSERT_EQ(2u, maps.size());
	EXPECT_EQ("MAP01", maps[0]);
	EXPECT_EQ("MAP02", maps[1]);
}

TEST(MapRotation, AQuotedNameStopsAtTheQuote)
{
	// Not support for quoting, which no cfg here uses. Just a guarantee that a quote cannot end up
	// inside a name that then goes out on a command line.
	const std::vector<std::string> maps = MapsInRotation("addmap \"MAP01\"\n");

	EXPECT_TRUE(maps.empty());
}

TEST(MapRotation, NothingElseInTheFileIsRead)
{
	// [rc4l] Deliberately not a cfg parser. It does not know what these are and must not act on them.
	const std::vector<std::string> maps = MapsInRotation(
		"exec somewhere.cfg\n"
		"sv_nojump true\n"
		"map MAP07\n"
		"addmap MAP01\n");

	ASSERT_EQ(1u, maps.size());
	EXPECT_EQ("MAP01", maps[0]);
}
