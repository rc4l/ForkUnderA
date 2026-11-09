// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <vector>

#include "features/iwad-startup/computation/iwadstartup_compute.h"

using zx::ComputeIsFreedoomIwad;
using zx::ComputeIwadSeenList;
using zx::ComputeIwadStartup;
using zx::IwadStartupCandidate;
using zx::IwadStartupChoice;

namespace
{

const IwadStartupCandidate kDoom2 = { "DOOM2", "doom.doom2.commercial" };
const IwadStartupCandidate kDoom = { "doom", "doom.id.doom1.ultimate" };
const IwadStartupCandidate kHeretic = { "heretic", "heretic.heretic" };
const IwadStartupCandidate kFree1 = { "freedoom1", "doom.freedoom.phase1" };
const IwadStartupCandidate kFree2 = { "freedoom2", "doom.freedoom.phase2" };
const IwadStartupCandidate kFreeDM = { "freedm", "doom.freedoom.freedm" };

std::vector<IwadStartupCandidate> List(std::initializer_list<IwadStartupCandidate> items)
{
	return std::vector<IwadStartupCandidate>(items);
}

} // namespace

TEST(IwadStartupCompute, FreedoomAutonamesAreRecognisedInAnyCase)
{
	EXPECT_TRUE(ComputeIsFreedoomIwad("doom.freedoom.phase2"));
	EXPECT_TRUE(ComputeIsFreedoomIwad("Doom.FreeDoom.FreeDM"));
	EXPECT_FALSE(ComputeIsFreedoomIwad("doom.doom2.commercial"));
	EXPECT_FALSE(ComputeIsFreedoomIwad("doom"));
	EXPECT_FALSE(ComputeIsFreedoomIwad(""));
}

TEST(IwadStartupCompute, NoneOrOneFoundNeverAsks)
{
	IwadStartupChoice c = ComputeIwadStartup(List({}), "", "", true);
	EXPECT_EQ(0, c.pick);
	EXPECT_FALSE(c.ask);

	c = ComputeIwadStartup(List({ kDoom2 }), "freedoom2", "", true);
	EXPECT_EQ(0, c.pick);
	EXPECT_FALSE(c.ask);
}

TEST(IwadStartupCompute, ShippedFreedoomAloneStartsPhaseTwoWithoutAsking)
{
	const IwadStartupChoice c = ComputeIwadStartup(List({ kFree1, kFree2 }), "", "", true);
	EXPECT_EQ(1, c.pick);
	EXPECT_FALSE(c.ask);
}

TEST(IwadStartupCompute, FreedoomAloneKeepsASavedFreedoomPick)
{
	const IwadStartupChoice c = ComputeIwadStartup(List({ kFree1, kFree2 }), "FREEDOOM1", "", true);
	EXPECT_EQ(0, c.pick);
	EXPECT_FALSE(c.ask);
}

TEST(IwadStartupCompute, FreedoomWithoutPhaseTwoStartsTheFirst)
{
	const IwadStartupChoice c = ComputeIwadStartup(List({ kFreeDM, kFree1 }), "", "", true);
	EXPECT_EQ(0, c.pick);
	EXPECT_FALSE(c.ask);
}

TEST(IwadStartupCompute, FirstPhaseTwoWinsWhenThereAreTwo)
{
	const IwadStartupCandidate other = { "freedoom2b", "doom.freedoom.phase2" };
	const IwadStartupChoice c = ComputeIwadStartup(List({ kFree1, kFree2, other }), "", "", false);
	EXPECT_EQ(1, c.pick);
}

TEST(IwadStartupCompute, Doom2BesideFreedoomAsksWithDoom2Preselected)
{
	const IwadStartupChoice c = ComputeIwadStartup(List({ kDoom2, kFree1, kFree2 }), "", "", true);
	EXPECT_EQ(0, c.pick);
	EXPECT_TRUE(c.ask);
}

TEST(IwadStartupCompute, NewDoom2OverridesASavedFreedoomPickAndAsksDespiteDontAsk)
{
	// [rc4l] The reported case: "don't ask" ticked while only Freedoom existed, then doom2.wad added.
	const IwadStartupChoice c = ComputeIwadStartup(List({ kDoom2, kFree1, kFree2 }), "freedoom2",
		"freedoom1;freedoom2", false);
	EXPECT_EQ(0, c.pick);
	EXPECT_TRUE(c.ask);
}

TEST(IwadStartupCompute, AFreedoomPickMadeWithDoom2ListedIsRespected)
{
	const IwadStartupChoice c = ComputeIwadStartup(List({ kDoom2, kFree1, kFree2 }), "freedoom2",
		"doom2;freedoom1;freedoom2", false);
	EXPECT_EQ(2, c.pick);
	EXPECT_FALSE(c.ask);
}

TEST(IwadStartupCompute, ASavedOwnIwadStaysPreselectedWhenAnotherIsNew)
{
	const IwadStartupChoice c = ComputeIwadStartup(List({ kDoom2, kHeretic, kFree2 }), "heretic",
		"heretic;freedoom2", false);
	EXPECT_EQ(1, c.pick);
	EXPECT_TRUE(c.ask);
}

TEST(IwadStartupCompute, NothingNewFollowsQueryiwadAndTheSavedPick)
{
	const std::string seen = "doom;doom2;freedoom2";
	IwadStartupChoice c = ComputeIwadStartup(List({ kDoom2, kDoom, kFree2 }), "doom", seen, true);
	EXPECT_EQ(1, c.pick);
	EXPECT_TRUE(c.ask);

	c = ComputeIwadStartup(List({ kDoom2, kDoom, kFree2 }), "doom", seen, false);
	EXPECT_EQ(1, c.pick);
	EXPECT_FALSE(c.ask);
}

TEST(IwadStartupCompute, NoSavedPickFallsToTheFirstOwnIwad)
{
	const IwadStartupChoice c = ComputeIwadStartup(List({ kFree1, kDoom2, kDoom }), "missing",
		"freedoom1;doom2;doom", false);
	EXPECT_EQ(1, c.pick);
	EXPECT_FALSE(c.ask);
}

TEST(IwadStartupCompute, TheSecondNewOwnIwadDoesNotDisplaceTheFirst)
{
	const IwadStartupChoice c = ComputeIwadStartup(List({ kDoom2, kDoom, kFree2 }), "", "", false);
	EXPECT_EQ(0, c.pick);
	EXPECT_TRUE(c.ask);
}

TEST(IwadStartupCompute, SeenListIsLowerCaseAndSemicolonSeparated)
{
	EXPECT_EQ("doom2;freedoom1;freedoom2", ComputeIwadSeenList(List({ kDoom2, kFree1, kFree2 })));
	EXPECT_EQ("", ComputeIwadSeenList(List({})));
}

TEST(IwadStartupCompute, FindsAFolderEntryInAnyCase)
{
	const std::vector<std::string> entries = { "readme.txt", "DOOM2.WAD", "doom2.wad" };
	EXPECT_EQ("DOOM2.WAD", zx::ComputeFindNameIgnoringCase(entries, "doom2.wad"));
	EXPECT_EQ("", zx::ComputeFindNameIgnoringCase(entries, "tnt.wad"));
	EXPECT_EQ("", zx::ComputeFindNameIgnoringCase(std::vector<std::string>(), "doom2.wad"));
}

TEST(IwadStartupCompute, SeenListMatchesWholeNamesOnly)
{
	// [rc4l] "doom" being seen must not count doom2 as seen.
	const IwadStartupChoice c = ComputeIwadStartup(List({ kDoom2, kDoom, kFree2 }), "doom",
		"doom;freedoom2", false);
	EXPECT_EQ(1, c.pick);
	EXPECT_TRUE(c.ask);
}
