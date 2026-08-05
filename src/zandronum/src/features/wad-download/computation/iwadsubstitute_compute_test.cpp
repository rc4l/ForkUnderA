// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/wad-download/computation/iwadallow_compute.h"
#include "features/wad-download/computation/iwadsubstitute_compute.h"

using zx::FreeIwadSubstituteFor;
using zx::IsFreeIwadName;
using std::string;

TEST(IwadSubstitute, DoomTwoAndFinalDoomFallBackToFreedoomPhaseTwo)
{
	// Same MAP01-MAP32 layout, so a PWAD authored against Doom II finds the map slots it expects.
	EXPECT_EQ("freedoom2.wad", FreeIwadSubstituteFor("doom2.wad"));
	EXPECT_EQ("freedoom2.wad", FreeIwadSubstituteFor("tnt.wad"));
	EXPECT_EQ("freedoom2.wad", FreeIwadSubstituteFor("plutonia.wad"));
}

TEST(IwadSubstitute, DoomOneFallsBackToFreedoomPhaseOne)
{
	// Episodic ExMy, not MAPxx -- substituting Phase 2 here would present the wrong map slots
	// entirely, so the two Freedoom halves are not interchangeable.
	EXPECT_EQ("freedoom1.wad", FreeIwadSubstituteFor("doom.wad"));
	EXPECT_EQ("freedoom1.wad", FreeIwadSubstituteFor("doom1.wad"));
	EXPECT_EQ("freedoom1.wad", FreeIwadSubstituteFor("doomu.wad"));
}

TEST(IwadSubstitute, CoversTheReReleaseFilenames)
{
	// The BFG/Unity editions ship the same game under other filenames; a server on one of those is
	// exactly as substitutable as a server on the original.
	EXPECT_EQ("freedoom2.wad", FreeIwadSubstituteFor("bfgdoom2.wad"));
	EXPECT_EQ("freedoom2.wad", FreeIwadSubstituteFor("doom2bfg.wad"));
	EXPECT_EQ("freedoom1.wad", FreeIwadSubstituteFor("bfgdoom.wad"));
	EXPECT_EQ("freedoom1.wad", FreeIwadSubstituteFor("doombfg.wad"));
}

TEST(IwadSubstitute, MatchesCaseInsensitively)
{
	// Servers spell filenames however their filesystem does.
	EXPECT_EQ("freedoom2.wad", FreeIwadSubstituteFor("DOOM2.WAD"));
	EXPECT_EQ("freedoom2.wad", FreeIwadSubstituteFor("Doom2.Wad"));
}

TEST(IwadSubstitute, HasNothingToOfferForGamesFreedoomDoesNotReplace)
{
	// Freedoom replaces Doom's data and nothing else. Guessing a stand-in for Heretic or Strife would
	// mean loading a game with the wrong actors, sounds and map format -- an empty answer, and the
	// caller's plain "you are missing this" message, is the honest outcome.
	EXPECT_EQ("", FreeIwadSubstituteFor("heretic.wad"));
	EXPECT_EQ("", FreeIwadSubstituteFor("hexen.wad"));
	EXPECT_EQ("", FreeIwadSubstituteFor("strife1.wad"));
	EXPECT_EQ("", FreeIwadSubstituteFor("doom64.wad"));
}

TEST(IwadSubstitute, HasNothingToOfferForAnIwadItHasNeverHeardOf)
{
	EXPECT_EQ("", FreeIwadSubstituteFor("brandnewgame2029.wad"));
	EXPECT_EQ("", FreeIwadSubstituteFor(""));
}

TEST(IwadSubstitute, DoesNotSubstituteAnythingForAFreeIwad)
{
	// A free IWAD is not a licence problem, so there is nothing to stand in for -- and quietly
	// swapping one free game for another would just be loading the wrong game.
	EXPECT_EQ("", FreeIwadSubstituteFor("freedoom2.wad"));
	EXPECT_EQ("", FreeIwadSubstituteFor("megagame.wad"));
	EXPECT_EQ("", FreeIwadSubstituteFor("chex3.wad"));
}

TEST(IwadSubstitute, EverySubstituteIsItselfDownloadable)
{
	// The property that makes substitution useful rather than a dead end: we tell the player "loading
	// freedoom2.wad instead", and if they do not have that either we must be able to fetch it. The
	// build already fails on a violation (gen-wadlists.cmake checks the pair against the allowlist);
	// this asserts it from the other side, against the same shipped tables the engine links.
	const char *const wanted[] = {
		"doom.wad", "doom1.wad", "doomu.wad", "bfgdoom.wad", "doombfg.wad",
		"doom2.wad", "doom2f.wad", "bfgdoom2.wad", "doom2bfg.wad", "tnt.wad", "plutonia.wad",
	};
	for (size_t i = 0; i < sizeof wanted / sizeof wanted[0]; ++i)
	{
		const string sub = FreeIwadSubstituteFor(wanted[i]);
		ASSERT_FALSE(sub.empty()) << wanted[i] << " lost its substitute";
		EXPECT_TRUE(IsFreeIwadName(sub)) << sub << " is not on the download allowlist";
	}
}
