// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/addon-catalogue/computation/maprotation_compute.h"

using zx::MapRotationStart;
using zx::MapsInRotation;

namespace
{

std::vector<std::string> Rotation(const char *a, const char *b = NULL, const char *c = NULL)
{
	std::vector<std::string> maps;
	maps.push_back(a);
	if (b) maps.push_back(b);
	if (c) maps.push_back(c);
	return maps;
}

} // namespace

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

// ------------------------------------------------------------ where it begins

TEST(MapRotationStart, AskingForNothingStartsAtTheTop)
{
	EXPECT_EQ(0u, MapRotationStart(Rotation("MAP01", "MAP02", "MAP03"), ""));
}

TEST(MapRotationStart, TheAskedForMapIsWhereItBegins)
{
	EXPECT_EQ(2u, MapRotationStart(Rotation("MAP01", "MAP02", "MAP03"), "MAP03"));
	EXPECT_EQ(1u, MapRotationStart(Rotation("MAP01", "MAP02", "MAP03"), "MAP02"));
}

TEST(MapRotationStart, TheNameIsMatchedEitherWay)
{
	// [rc4l] A rotation is written by hand and a map name is a lump name. The catalogue holds
	// `addmap Gvh00` and `addmap D2CTF1` in the same breath, and the engine's own comparisons of
	// level names have never been case-sensitive.
	EXPECT_EQ(1u, MapRotationStart(Rotation("D2CTF1", "D2CTF2"), "d2ctf2"));
	EXPECT_EQ(1u, MapRotationStart(Rotation("Gvh00", "Gvh01"), "GVH01"));
}

TEST(MapRotationStart, AMapTheRotationDoesNotHoldStartsAtTheTop)
{
	// Where it would have started. An unmet request is not worth refusing a server over, and there
	// is nowhere to say so by the time this is asked.
	EXPECT_EQ(0u, MapRotationStart(Rotation("MAP01", "MAP02"), "MAP31"));
	EXPECT_EQ(0u, MapRotationStart(std::vector<std::string>(), "MAP01"));
}

TEST(MapRotationStart, APrefixIsNotAMatch)
{
	// MAP1 must not find MAP15, or a rotation would begin somewhere nobody named.
	EXPECT_EQ(0u, MapRotationStart(Rotation("MAP15", "MAP16"), "MAP1"));
	EXPECT_EQ(0u, MapRotationStart(Rotation("MAP15", "MAP16"), "MAP150"));
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
