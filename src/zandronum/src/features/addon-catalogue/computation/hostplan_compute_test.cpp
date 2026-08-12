// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/hostplan_compute.h"

#include <string>
#include <vector>

using zx::AddonEntry;
using zx::AddonFileRef;
using zx::BuildHostPlan;
using zx::HostChoices;
using zx::HostPlan;
using zx::IwadChoice;
using zx::IwadPick;

namespace
{

// Duel 40 as it actually ships: two files, the announcer after the maps, wanting doom2.
AddonEntry Duel40()
{
	AddonEntry a;
	a.valid = true;
	a.id = "duel40";
	a.name = "Duel 40";
	a.iwad = "doom2.wad";

	AddonFileRef maps;
	maps.name = "duel40b.pk3";
	maps.md5 = "aa3896cb47c781facab7ea7f39395201";
	a.files.push_back(maps);

	AddonFileRef spree;
	spree.name = "zandrospree2rc2.pk3";
	spree.md5 = "25b2a3c4f46e50f4016b640119aefae6";
	a.files.push_back(spree);

	return a;
}

IwadPick Picked(IwadChoice choice, const char *iwad)
{
	IwadPick p;
	p.choice = choice;
	p.iwad = iwad;
	p.wanted = "doom2.wad";
	return p;
}

HostChoices Mine()
{
	HostChoices c;
	c.serverName = "My Server";
	c.maxPlayers = 4;
	c.port = 10666;
	c.advertise = true;
	return c;
}

std::vector<std::string> Files(const char *a = 0, const char *b = 0)
{
	std::vector<std::string> v;
	if (a) v.push_back(a);
	if (b) v.push_back(b);
	return v;
}

// A pack that plays one way loads exactly its own files, which is what PickVariant resolves for it.
// Spelling that out once keeps these cases about planning rather than about resolution.
HostPlan PlanFor(const AddonEntry &addon, const IwadPick &iwad, const std::string &cfg,
	const HostChoices &choices, const std::vector<std::string> &haveFiles)
{
	return BuildHostPlan(addon, addon.files, iwad, cfg, std::vector<std::string>(), addon.map,
		choices, haveFiles);
}

AddonFileRef Ref(const char *name, const char *md5)
{
	AddonFileRef f;
	f.name = name;
	f.md5 = md5;
	return f;
}

} // namespace

// ---------------------------------------------------------------- the whole point

TEST(HostPlan, EverythingPresentIsReadyToStart)
{
	const HostPlan p = PlanFor(Duel40(),
		Picked(IwadChoice::Substitute, "freedoom2.wad"),
		"catalogue/duel40/server.cfg", Mine(),
		Files("duel40b.pk3", "zandrospree2rc2.pk3"));

	EXPECT_TRUE(p.ready);
	EXPECT_TRUE(p.missing.empty());
	EXPECT_TRUE(p.blocker.empty());

	EXPECT_EQ("freedoom2.wad", p.iwad);
	EXPECT_EQ("catalogue/duel40/server.cfg", p.execCfg);

	ASSERT_EQ(2u, p.pwads.size());
	EXPECT_EQ("duel40b.pk3", p.pwads[0]);
	EXPECT_EQ("zandrospree2rc2.pk3", p.pwads[1]);
}

TEST(HostPlan, TheHostOwnsTheNameAndTheEntryOwnsTheFiles)
{
	// An entry never names the server or picks the port. Those belong to whoever is running it.
	const HostPlan p = PlanFor(Duel40(),
		Picked(IwadChoice::Preferred, "doom2.wad"),
		"", Mine(), Files("duel40b.pk3", "zandrospree2rc2.pk3"));

	EXPECT_EQ("My Server", p.serverName);
	EXPECT_EQ(4, p.maxPlayers);
	EXPECT_EQ(10666, p.port);
	EXPECT_TRUE(p.advertise);
}

TEST(HostPlan, LoadOrderIsTheEntrysAndIsNotSecondGuessed)
{
	// The order in the file is the only thing saying the announcer goes after the maps.
	const HostPlan p = PlanFor(Duel40(),
		Picked(IwadChoice::Preferred, "doom2.wad"),
		"", Mine(), Files("zandrospree2rc2.pk3", "duel40b.pk3"));

	ASSERT_EQ(2u, p.pwads.size());
	EXPECT_EQ("duel40b.pk3", p.pwads[0]) << "having them on disk in another order changes nothing";
}

// ---------------------------------------------------------------- downloads, not refusals

TEST(HostPlan, AMissingPwadIsADownloadRatherThanARefusal)
{
	// Shipping the hashes is what makes fetching possible, so treating an absent file as fatal would
	// waste the whole design.
	const HostPlan p = PlanFor(Duel40(),
		Picked(IwadChoice::Preferred, "doom2.wad"),
		"", Mine(), Files("duel40b.pk3"));

	EXPECT_FALSE(p.ready);
	EXPECT_TRUE(p.blocker.empty()) << "not a blocker; it is a download";

	ASSERT_EQ(1u, p.missing.size());
	EXPECT_EQ("zandrospree2rc2.pk3", p.missing[0]);
}

TEST(HostPlan, TheFullFileListSurvivesEvenWhenSomeAreMissing)
{
	// The caller downloads `missing` and then starts with `pwads`, so the plan has to carry both.
	const HostPlan p = PlanFor(Duel40(),
		Picked(IwadChoice::Preferred, "doom2.wad"), "", Mine(), Files());

	EXPECT_EQ(2u, p.pwads.size());
	EXPECT_EQ(2u, p.missing.size());
}

TEST(HostPlan, MissingFilesKeepLoadOrderSoTheProgressReadsSensibly)
{
	const HostPlan p = PlanFor(Duel40(),
		Picked(IwadChoice::Preferred, "doom2.wad"), "", Mine(), Files());

	ASSERT_EQ(2u, p.missing.size());
	EXPECT_EQ("duel40b.pk3", p.missing[0]);
}

// ---------------------------------------------------------------- what actually blocks

TEST(HostPlan, NoIwadIsABlockerBecauseNothingCanBeFetchedForIt)
{
	// PickIwad has already tried the substitute table, so None means there is genuinely nothing to
	// run on, and offering a download would be offering a commercial game we may not fetch.
	const HostPlan p = PlanFor(Duel40(),
		Picked(IwadChoice::None, ""), "", Mine(),
		Files("duel40b.pk3", "zandrospree2rc2.pk3"));

	EXPECT_FALSE(p.ready);
	EXPECT_FALSE(p.blocker.empty());
	EXPECT_NE(std::string::npos, p.blocker.find("doom2.wad")) << "say WHICH game is missing";
}

TEST(HostPlan, AnUnreadableEntryIsABlockerAndNotAnEmptyServer)
{
	AddonEntry broken;
	broken.valid = false;
	broken.error = "no files";

	const HostPlan p = PlanFor(broken,
		Picked(IwadChoice::Preferred, "doom2.wad"), "", Mine(), Files());

	EXPECT_FALSE(p.ready);
	EXPECT_FALSE(p.blocker.empty());
	EXPECT_TRUE(p.pwads.empty());
}

// ---------------------------------------------------------------- edges

TEST(HostPlan, AnEntryWithNoConfigExecsNothing)
{
	const HostPlan p = PlanFor(Duel40(),
		Picked(IwadChoice::Preferred, "doom2.wad"), "", Mine(),
		Files("duel40b.pk3", "zandrospree2rc2.pk3"));

	EXPECT_TRUE(p.execCfg.empty());
	EXPECT_TRUE(p.ready) << "a missing cfg is not a problem; most entries will not have one";
}

TEST(HostPlan, CaseDoesNotDecideWhetherYouHaveAFile)
{
	// A file off a Windows disk is the same file whatever its spelling.
	const HostPlan p = PlanFor(Duel40(),
		Picked(IwadChoice::Preferred, "doom2.wad"), "", Mine(),
		Files("DUEL40B.PK3", "ZandroSpree2RC2.pk3"));

	EXPECT_TRUE(p.ready);
	EXPECT_TRUE(p.missing.empty());
}

TEST(HostPlan, AnEntryThatNamesNoIwadStillSaysWhyItCannotRun)
{
	// [rc4l] The other half of the blocker. An entry that names a game gets "you need doom2.wad";
	// one that names none has nothing to name, and saying "you need " with a blank after it would be
	// worse than saying nothing.
	AddonEntry anyGame = Duel40();
	anyGame.iwad = "";

	IwadPick nothing;
	nothing.choice = IwadChoice::None;

	const HostPlan p = PlanFor(anyGame, nothing, "", Mine(),
		Files("duel40b.pk3", "zandrospree2rc2.pk3"));

	EXPECT_FALSE(p.ready);
	EXPECT_EQ("no IWAD to run on", p.blocker);
}

TEST(HostPlan, TwoNamesOfTheSameLengthAreStillDifferentFiles)
{
	// Same length, so the cheap size test cannot separate them and the comparison has to actually
	// read the characters.
	AddonEntry a = Duel40();
	a.files.resize(1);
	a.files[0].name = "aaaaaaaa.pk3";

	const HostPlan p = PlanFor(a,
		Picked(IwadChoice::Preferred, "doom2.wad"), "", Mine(),
		Files("bbbbbbbb.pk3"));

	EXPECT_FALSE(p.ready);
	ASSERT_EQ(1u, p.missing.size());
	EXPECT_EQ("aaaaaaaa.pk3", p.missing[0]);
}

// ---------------------------------------------------------------- what a variant loads

TEST(HostPlan, ItPlansTheFilesItIsGivenAndNotTheEntrysOwn)
{
	// [rc4l] Ghouls vs Humans has nothing at the entry level and a different wad per way of playing,
	// so an entry no longer has one answer to "what does this load". Reading addon.files here would
	// plan for whichever way of playing happened to be listed first.
	AddonEntry gvh;
	gvh.valid = true;
	gvh.id = "gvh";
	gvh.iwad = "doom2.wad";

	std::vector<AddonFileRef> variant;
	variant.push_back(Ref("gvh-maps.pk3", "aa3896cb47c781facab7ea7f39395201"));

	const HostPlan p = BuildHostPlan(gvh, variant,
		Picked(IwadChoice::Preferred, "doom2.wad"), "", std::vector<std::string>(), "", Mine(), Files("gvh-maps.pk3"));

	EXPECT_TRUE(p.ready);
	ASSERT_EQ(1u, p.pwads.size());
	EXPECT_EQ("gvh-maps.pk3", p.pwads[0]);
}

TEST(HostPlan, AVariantsMissingWadIsStillJustADownload)
{
	// The variant's own files are fetched exactly like the entry's; being peculiar to one way of
	// playing does not make an absent file fatal.
	std::vector<AddonFileRef> resolved = Duel40().files;
	resolved.push_back(Ref("skulltag_announcer.pk3", "25b2a3c4f46e50f4016b640119aefae7"));

	const HostPlan p = BuildHostPlan(Duel40(), resolved,
		Picked(IwadChoice::Preferred, "doom2.wad"), "", std::vector<std::string>(), "", Mine(),
		Files("duel40b.pk3", "zandrospree2rc2.pk3"));

	EXPECT_FALSE(p.ready);
	EXPECT_TRUE(p.blocker.empty());
	ASSERT_EQ(1u, p.missing.size());
	EXPECT_EQ("skulltag_announcer.pk3", p.missing[0]);
	EXPECT_EQ(3u, p.pwads.size()) << "the variant's file is part of what gets started";
}

TEST(HostPlan, TheRemixCfgsRideAlongInOrder)
{
	// [rc4l] One per AXIS, and the order is the catalogue author's: a mod named after a rules remix
	// execs after it and so wins where the two overlap, which is the only way to say that at all.
	std::vector<std::string> remixes;
	remixes.push_back("/cat/remix/teamdm/teamdm.cfg");
	remixes.push_back("/cat/remix/dnd/dnd.cfg");

	HostChoices choices;
	choices.serverName = "A server";
	choices.maxPlayers = 8;
	choices.port = 10666;

	const HostPlan plan = BuildHostPlan(Duel40(), Duel40().files,
		Picked(IwadChoice::Preferred, "doom2.wad"), "/cat/duel40/server.cfg", remixes, "START",
		choices, std::vector<std::string>());

	ASSERT_EQ(2u, plan.execRemixCfgs.size());
	EXPECT_EQ("/cat/remix/teamdm/teamdm.cfg", plan.execRemixCfgs[0]);
	EXPECT_EQ("/cat/remix/dnd/dnd.cfg", plan.execRemixCfgs[1]);
}

TEST(HostPlan, AnAxisWithNoCfgOfItsOwnAddsNoExec)
{
	// The baseline of an axis changes nothing and so ships no cfg. It still comes through the list
	// as an empty string, because the list is one entry per axis and dropping it here would make
	// the positions mean something different from what the picker chose.
	std::vector<std::string> remixes;
	remixes.push_back("");
	remixes.push_back("/cat/remix/dnd/dnd.cfg");

	HostChoices choices;
	choices.serverName = "A server";
	choices.maxPlayers = 8;
	choices.port = 10666;

	const HostPlan plan = BuildHostPlan(Duel40(), Duel40().files,
		Picked(IwadChoice::Preferred, "doom2.wad"), "/cat/duel40/server.cfg", remixes, "START",
		choices, std::vector<std::string>());

	ASSERT_EQ(1u, plan.execRemixCfgs.size());
	EXPECT_EQ("/cat/remix/dnd/dnd.cfg", plan.execRemixCfgs[0]);
}
