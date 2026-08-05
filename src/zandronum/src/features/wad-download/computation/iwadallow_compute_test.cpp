// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <cstring>

#include "gtest/gtest.h"
#include "features/wad-download/computation/iwadallow_compute.h"

using zx::ClassifyDownloadedFile;
using zx::ClassifyWantedFile;
using zx::DownloadVerdict;
using zx::DownloadVerdictReason;
using zx::HeaderIsIwadMagic;
using zx::IsFreeIwadName;
using std::string;


namespace
{
// The first bytes of a real Doom IWAD directory header, and of the things that are not one.
const char kIwadHeader[] = { 'I', 'W', 'A', 'D', 0x10, 0, 0, 0 };
const char kPwadHeader[] = { 'P', 'W', 'A', 'D', 0x10, 0, 0, 0 };
const char kPk3Header[]  = { 'P', 'K', 0x03, 0x04, 0, 0, 0, 0 };

DownloadVerdict WantIwad(const string &name) { return ClassifyWantedFile(name, true); }
DownloadVerdict WantPwad(const string &name) { return ClassifyWantedFile(name, false); }
} // namespace

//=============================================================================
// The core rule: every IWAD is assumed commercial until the allowlist says otherwise
//=============================================================================

TEST(IwadAllow, RefusesAnIwadItCannotConfirmIsFree)
{
	// The whole design in one case. We hold no register of commercial games, so this is refused for
	// what we DON'T know about it, not for what we do -- which is also why a game released after this
	// build shipped is refused just as reliably as one that predates it.
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("brandnewgame2029.wad"));
}

TEST(IwadAllow, RefusesTheCommercialGamesWithoutKnowingTheyAreCommercial)
{
	// Not on any denylist -- there isn't one. They are refused because they are IWADs that are not on
	// the free list, which is the same reason brandnewgame2029.wad is.
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("doom.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("doom2.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("plutonia.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("tnt.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("heretic.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("hexen.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("strife1.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("doom64.wad"));
	// Including the re-releases, which ship the same maps under other filenames. A denylist would
	// have to enumerate every one of these; deny-by-default never has to hear about them.
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("bfgdoom2.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("doom2bfg.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("doom_complete.pk3"));
}

TEST(IwadAllow, RefusesThePaidEditionOfAGameWhoseOriginalIsFree)
{
	// REKKR shipped free; "Sunken Land" is the paid Steam edition under its own filename. Leaving the
	// paid filename off the list is the entire mechanism -- no second list to keep in step.
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("rekkr.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("rekkrsa.wad"));
}

TEST(IwadAllow, AllowsTheFreeIwadsItKnows)
{
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("freedoom1.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("freedoom2.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("freedoomu.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("freedm.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("blasphem.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("blasphemer.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("hacx.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("hacx2.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("harm1.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("action2.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("square1.pk3"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("rekkr.wad"));
}

TEST(IwadAllow, AllowsTheChexQuestIwads)
{
	// Never sold: Chex Quest was a cereal-box giveaway, and both sequels were released free by their
	// authors. All three, plus the Vanilla edition of Chex Quest 3.
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("chex.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("chex2.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("chex3.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("chex3v.wad"));
}

TEST(IwadAllow, AllowsMegaManEightBitDeathmatch)
{
	// A freeware total conversion, and one of the busiest things on the Zandronum browser -- joining
	// one of those servers is the case this whole feature exists for.
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("megagame.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantPwad("mm8bdm-v6b.pk3"));
}

TEST(IwadAllow, TheNameCheckIsCaseInsensitive)
{
	// Servers spell filenames however their filesystem does; the gate must not be dodgeable by shift.
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("DOOM2.WAD"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("Doom2.Wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantIwad("FreeDoom2.WAD"));
}

//=============================================================================
// PWADs
//=============================================================================

TEST(IwadAllow, OrdinaryPwadsAreAllowedWithoutBeingOnAnyList)
{
	// PWADs are mods; deny-by-default there would mean downloading nothing at all.
	EXPECT_EQ(DownloadVerdict::Allowed, WantPwad("brutal.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantPwad("eviternity.wad"));
	EXPECT_EQ(DownloadVerdict::Allowed, WantPwad("skulltag_actors.pk3"));
}

TEST(IwadAllow, AGameListedAsAPwadPassesTheNameGateAndIsCaughtByTheContentGate)
{
	// Documents where the evasion is stopped. Declaring doom2.wad as a "PWAD" gets past the pre-fetch
	// check -- there is no name list to catch it -- and is then refused for what actually arrived.
	EXPECT_EQ(DownloadVerdict::Allowed, WantPwad("doom2.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad,
		ClassifyDownloadedFile("doom2.wad", kIwadHeader, sizeof kIwadHeader));
}

//=============================================================================
// The list itself
//=============================================================================

TEST(IwadAllow, TheAllowlistIsTheOneCompiledInFromTheRepoFile)
{
	// These assertions run against the SAME generated header the engine links, produced from
	// iwadallowlist.txt by tools/gen-wadlists.cmake -- so deleting a line from that file fails here
	// rather than silently shipping a narrower gate, and adding a commercial IWAD to it fails the
	// tests below rather than silently shipping a wider one.
	EXPECT_TRUE(IsFreeIwadName("freedoom2.wad"));
	EXPECT_TRUE(IsFreeIwadName("megagame.wad"));
	EXPECT_FALSE(IsFreeIwadName("doom2.wad"));
	EXPECT_FALSE(IsFreeIwadName("rekkrsa.wad"));
}

TEST(IwadAllow, ThereIsNoWayToWidenTheGateAtRuntime)
{
	// Not an assertion about behaviour so much as about the SHAPE of the API: ClassifyWantedFile and
	// IsFreeIwadName take a name and nothing else. An earlier draft had a CVAR to extend the list,
	// and the first thing anyone would have written to it is "doom2.wad" -- pasted from a server
	// setup guide, into a config nobody reads. This test exists so that re-adding such a parameter
	// has to break a test that says why it was removed.
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("doom2.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad, WantIwad("brandnewgame2029.wad"));
}

//=============================================================================
// The content gate -- the only check a rename cannot walk past
//=============================================================================

TEST(HeaderIsIwadMagic, RecognisesAnIwadDirectoryHeader)
{
	EXPECT_TRUE(HeaderIsIwadMagic(kIwadHeader, sizeof kIwadHeader));
	EXPECT_FALSE(HeaderIsIwadMagic(kPwadHeader, sizeof kPwadHeader));
	EXPECT_FALSE(HeaderIsIwadMagic(kPk3Header, sizeof kPk3Header));
}

TEST(HeaderIsIwadMagic, AFileTooShortToHaveAHeaderIsNotAnIwad)
{
	EXPECT_FALSE(HeaderIsIwadMagic("IWA", 3));
	EXPECT_FALSE(HeaderIsIwadMagic("", 0));
	EXPECT_FALSE(HeaderIsIwadMagic(NULL, 8));
}

TEST(DownloadedFile, RefusesIwadContentArrivingUnderAnInnocentName)
{
	// doom2.wad renamed to coolmod.wad, requested as a PWAD. The name says nothing; only reading what
	// actually arrived catches it.
	EXPECT_EQ(DownloadVerdict::Allowed, WantPwad("coolmod.wad"));
	EXPECT_EQ(DownloadVerdict::UnlistedIwad,
		ClassifyDownloadedFile("coolmod.wad", kIwadHeader, sizeof kIwadHeader));
}

TEST(DownloadedFile, KeepsAnOrdinaryPwad)
{
	EXPECT_EQ(DownloadVerdict::Allowed,
		ClassifyDownloadedFile("brutal.wad", kPwadHeader, sizeof kPwadHeader));
	EXPECT_EQ(DownloadVerdict::Allowed,
		ClassifyDownloadedFile("skins.pk3", kPk3Header, sizeof kPk3Header));
}

TEST(DownloadedFile, KeepsAnAllowlistedIwadThatReallyIsAnIwad)
{
	EXPECT_EQ(DownloadVerdict::Allowed,
		ClassifyDownloadedFile("freedoom2.wad", kIwadHeader, sizeof kIwadHeader));
	EXPECT_EQ(DownloadVerdict::Allowed,
		ClassifyDownloadedFile("megagame.wad", kIwadHeader, sizeof kIwadHeader));
}

TEST(DownloadedFile, RechecksTheNameRatherThanTrustingTheEarlierPass)
{
	// This is the last gate before a file is kept, so it has to be safe to call on a file that
	// arrived by any route -- including one that never went through ClassifyWantedFile.
	EXPECT_EQ(DownloadVerdict::UnsafeName,
		ClassifyDownloadedFile("../evil.wad", kPwadHeader, sizeof kPwadHeader));
	EXPECT_EQ(DownloadVerdict::UnsafeName,
		ClassifyDownloadedFile("evil.exe", kPwadHeader, sizeof kPwadHeader));
}

TEST(DownloadedFile, AnEmptyOrTruncatedFileIsNotMistakenForAnIwad)
{
	EXPECT_EQ(DownloadVerdict::Allowed, ClassifyDownloadedFile("brutal.wad", "", 0));
}

//=============================================================================
// Names and reasons
//=============================================================================

TEST(IwadAllow, UnsafeNamesAreRejectedBeforeAnyLegalQuestionIsAsked)
{
	EXPECT_EQ(DownloadVerdict::UnsafeName, WantPwad("../../doom2.wad"));
	EXPECT_EQ(DownloadVerdict::UnsafeName, WantIwad("evil.exe"));
	EXPECT_EQ(DownloadVerdict::UnsafeName, WantIwad(""));
}

TEST(IwadAllow, EveryVerdictHasSomethingToShowThePlayer)
{
	const DownloadVerdict all[] = { DownloadVerdict::Allowed, DownloadVerdict::UnsafeName,
		DownloadVerdict::UnlistedIwad };
	for (size_t i = 0; i < sizeof all / sizeof all[0]; ++i)
	{
		const char *reason = DownloadVerdictReason(all[i]);
		ASSERT_TRUE(reason != NULL);
		EXPECT_GT(strlen(reason), 0u);
	}
}
