// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/server-browser/computation/maplist_compute.h"

using namespace zx;

namespace
{

LumpEntry Lump(const char *name)
{
	return LumpEntry(name, "");
}

LumpEntry File(const char *path)
{
	return LumpEntry("", path);
}

std::vector<LumpEntry> DoomWad()
{
	// A map header is an empty lump followed by its data. Two maps, and a texture lump between
	// them that must not be mistaken for one.
	std::vector<LumpEntry> out;
	out.push_back(Lump("MAP01"));
	out.push_back(Lump("THINGS"));
	out.push_back(Lump("LINEDEFS"));
	out.push_back(Lump("SIDEDEFS"));
	out.push_back(Lump("SECTORS"));
	out.push_back(Lump("PNAMES"));
	out.push_back(Lump("MAP02"));
	out.push_back(Lump("THINGS"));
	out.push_back(Lump("LINEDEFS"));

	return out;
}

} // namespace

// ---------------------------------------------------------------- the WAD rule

TEST(MapsInFile, FindsTheHeaderBeforeTheMapsOwnData)
{
	const std::vector<std::string> maps = MapsInFile(DoomWad());

	ASSERT_EQ(size_t(2), maps.size());
	EXPECT_EQ("MAP01", maps[0]);
	EXPECT_EQ("MAP02", maps[1]);
}

TEST(MapsInFile, TakesUdmfMapsToo)
{
	std::vector<LumpEntry> lumps;
	lumps.push_back(Lump("E1M1"));
	lumps.push_back(Lump("TEXTMAP"));
	lumps.push_back(Lump("ENDMAP"));

	const std::vector<std::string> maps = MapsInFile(lumps);

	ASSERT_EQ(size_t(1), maps.size());
	EXPECT_EQ("E1M1", maps[0]);
}

TEST(MapsInFile, IsNotFooledByALumpCalledThingsOnItsOwn)
{
	// A lump named THINGS with nothing before it is not a map, and the lump before a SECTORS is
	// not one either: only the two markers that actually START a map count.
	std::vector<LumpEntry> lumps;
	lumps.push_back(Lump("D_RUNNIN"));
	lumps.push_back(Lump("SECTORS"));
	lumps.push_back(Lump("CREDIT"));

	EXPECT_TRUE(MapsInFile(lumps).empty());
}

TEST(MapsInFile, IgnoresAHeaderNameNoMapCouldHave)
{
	std::vector<LumpEntry> lumps;
	lumps.push_back(Lump("BAD NAME!"));
	lumps.push_back(Lump("THINGS"));

	EXPECT_TRUE(MapsInFile(lumps).empty());
}

TEST(MapsInFile, ReadsTheLastLumpWithoutRunningOffTheEnd)
{
	std::vector<LumpEntry> lumps;
	lumps.push_back(Lump("MAP01"));

	EXPECT_TRUE(MapsInFile(lumps).empty());
}

// ---------------------------------------------------------------- the archive rule

TEST(MapsInFile, TakesAPk3sMapsFromTheirPaths)
{
	std::vector<LumpEntry> lumps;
	lumps.push_back(File("maps/MAP01.wad"));
	lumps.push_back(File("maps/map02.wad"));
	lumps.push_back(File("graphics/TITLEPIC.png"));
	lumps.push_back(File("MAPINFO"));

	const std::vector<std::string> maps = MapsInFile(lumps);

	ASSERT_EQ(size_t(2), maps.size());
	EXPECT_EQ("MAP01", maps[0]);
	EXPECT_EQ("MAP02", maps[1]) << "the engine does not care about the case of a path";
}

TEST(MapsInFile, LooksOnlyWhereTheEngineLooks)
{
	// A wad nested deeper is not a map the engine would load, so listing it would offer a rotation
	// entry that fails when the server reaches it.
	std::vector<LumpEntry> lumps;
	lumps.push_back(File("maps/doom2/MAP01.wad"));
	lumps.push_back(File("extras/MAP02.wad"));
	lumps.push_back(File("maps/README.txt"));

	EXPECT_TRUE(MapsInFile(lumps).empty());
}

TEST(MapsInFile, NamesEachMapOnce)
{
	std::vector<LumpEntry> lumps;
	lumps.push_back(Lump("MAP01"));
	lumps.push_back(Lump("THINGS"));
	lumps.push_back(Lump("MAP01"));
	lumps.push_back(Lump("THINGS"));

	ASSERT_EQ(size_t(1), MapsInFile(lumps).size());
}

// ---------------------------------------------------------------- across files

TEST(MergeMaps, KeepsThePositionAMapFirstAppearedIn)
{
	// A pwad that replaces MAP01 does not add a second visit to it.
	std::vector<std::string> rotation;
	rotation.push_back("MAP01");
	rotation.push_back("MAP02");

	std::vector<std::string> incoming;
	incoming.push_back("MAP01");
	incoming.push_back("MAP31");

	MergeMaps(rotation, incoming);

	ASSERT_EQ(size_t(3), rotation.size());
	EXPECT_EQ("MAP01", rotation[0]);
	EXPECT_EQ("MAP02", rotation[1]);
	EXPECT_EQ("MAP31", rotation[2]);
}

TEST(IsMapName, RefusesWhatCannotBeALumpName)
{
	EXPECT_TRUE(IsMapName("MAP01"));
	EXPECT_TRUE(IsMapName("E1M1"));
	EXPECT_TRUE(IsMapName("TEST_MAP"));

	EXPECT_FALSE(IsMapName(""));
	EXPECT_FALSE(IsMapName("TOOLONGNAME"));
	EXPECT_FALSE(IsMapName("MAP 01"));
	EXPECT_FALSE(IsMapName("-warp"));
	EXPECT_FALSE(IsMapName("MAP;01"));
}
