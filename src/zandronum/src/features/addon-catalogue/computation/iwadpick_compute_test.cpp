// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/iwadpick_compute.h"

#include <string>
#include <vector>

using zx::IwadChoice;
using zx::IwadPick;
using zx::PickIwad;

namespace
{

std::vector<std::string> Have(const char *a = 0, const char *b = 0)
{
	std::vector<std::string> v;
	if (a) v.push_back(a);
	if (b) v.push_back(b);
	return v;
}

} // namespace

TEST(IwadPick, OwningTheRealThingSettlesIt)
{
	// The substitute table must never be consulted first, or a player is handed Freedoom while their
	// Doom II sits on the disk.
	const IwadPick p = PickIwad("doom2.wad", Have("doom2.wad", "freedoom2.wad"));

	EXPECT_EQ(IwadChoice::Preferred, p.choice);
	EXPECT_EQ("doom2.wad", p.iwad);
	EXPECT_EQ("doom2.wad", p.wanted);
}

TEST(IwadPick, FreedoomStandsInWhenTheRealThingIsAbsent)
{
	// Naming the substitute is the whole story. Under duel40b the IWAD only supplies textures, sounds
	// and actors; under a gameplay mod alone the levels come from it too, and "freedoom2.wad" already
	// says whose levels those will be.
	const IwadPick p = PickIwad("doom2.wad", Have("freedoom2.wad"));

	EXPECT_EQ(IwadChoice::Substitute, p.choice);
	EXPECT_EQ("freedoom2.wad", p.iwad);
	EXPECT_EQ("doom2.wad", p.wanted);
}

TEST(IwadPick, NeitherPresentMeansItCannotBeHosted)
{
	const IwadPick p = PickIwad("doom2.wad", Have());

	EXPECT_EQ(IwadChoice::None, p.choice);
	EXPECT_TRUE(p.iwad.empty());
	EXPECT_EQ("doom2.wad", p.wanted) << "the caller still needs to say WHICH iwad is missing";
}

TEST(IwadPick, CaseDoesNotDecideWhetherYouOwnAGame)
{
	// doom2.WAD off a Windows disk is the same file to everyone except a string compare.
	const IwadPick p = PickIwad("doom2.wad", Have("DOOM2.WAD"));

	EXPECT_EQ(IwadChoice::Preferred, p.choice);
}

TEST(IwadPick, AnIwadWithNoSubstituteIsNotInvented)
{
	// Nothing stands in for a game the table has never heard of.
	const IwadPick p = PickIwad("madeupgame.wad", Have("freedoom2.wad"));

	EXPECT_EQ(IwadChoice::None, p.choice);
	EXPECT_TRUE(p.iwad.empty());
}

TEST(IwadPick, TheUltimateDoomSubstitutesToFreedoomPhaseOne)
{
	// The table is per-game, not one blanket answer, so phase 1 and phase 2 do not get swapped.
	const IwadPick p = PickIwad("doom.wad", Have("freedoom1.wad"));

	EXPECT_EQ(IwadChoice::Substitute, p.choice);
	EXPECT_EQ("freedoom1.wad", p.iwad);
}

TEST(IwadPick, HavingThePhaseOneStandInDoesNotCoverAPhaseTwoGame)
{
	// freedoom1 is not a stand-in for Doom II, and quietly accepting it would produce a server whose
	// maps are missing.
	const IwadPick p = PickIwad("doom2.wad", Have("freedoom1.wad"));

	EXPECT_EQ(IwadChoice::None, p.choice);
}

TEST(IwadPick, AskingForNothingYieldsNothing)
{
	const IwadPick p = PickIwad("", Have("freedoom2.wad"));

	EXPECT_EQ(IwadChoice::None, p.choice);
}
