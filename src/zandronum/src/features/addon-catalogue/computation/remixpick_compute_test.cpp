// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/remixpick_compute.h"

using zx::AddonEntry;
using zx::AddonFileRef;
using zx::AddonRemix;
using zx::AddonVariant;
using zx::CombineFiles;
using zx::GroupRemixes;
using zx::OfferedRemixes;
using zx::PickRemix;
using zx::PickRemixes;
using zx::RemixGroup;
using zx::RemixPick;

namespace
{

AddonRemix Remix(const std::string &id, const std::string &name, bool valid = true)
{
	AddonRemix r;
	r.id = id;
	r.name = name;
	r.valid = valid;
	return r;
}

AddonRemix Grouped(const std::string &id, const std::string &name, const std::string &group)
{
	AddonRemix r = Remix(id, name);
	r.group = group;
	return r;
}

AddonRemix Mode(const std::string &id, const std::string &name, zx::HostGameMode mode)
{
	AddonRemix r = Grouped(id, name, "mode");
	r.gameMode = mode;
	return r;
}

std::pair<std::string, std::string> Want(const std::string &group, const std::string &id)
{
	return std::make_pair(group, id);
}

AddonFileRef File(const std::string &name)
{
	AddonFileRef f;
	f.name = name;
	return f;
}

// The pool as it comes off disk: a folder listing, so alphabetical, and deliberately NOT in the
// order any entry names them.
std::vector<AddonRemix> Pool()
{
	std::vector<AddonRemix> pool;
	pool.push_back(Remix("classic", "Classic"));
	pool.push_back(Remix("survival", "Survival"));
	pool.push_back(Remix("teamdm", "Team Deathmatch"));
	return pool;
}

AddonEntry EntryWith(const std::vector<std::string> &remixes)
{
	AddonEntry e;
	e.remixes = remixes;
	return e;
}

} // namespace

// ------------------------------------------------------------ what is on offer

TEST(OfferedRemixes, AnEntryNamingNoneOffersNone)
{
	// The ordinary pack that plays one way. The caller draws no button at all for this.
	const AddonEntry entry;

	EXPECT_TRUE(OfferedRemixes(entry, -1, Pool()).empty());
}

TEST(OfferedRemixes, TheEntrysWrittenOrderIsKeptNotThePools)
{
	// [rc4l] The pool is a folder listing and arrives alphabetical; the list on the entry is written
	// by hand with the baseline first. "As it ships" has to stay at the top of the picker whatever it
	// is called, so the pool's order must not leak through.
	std::vector<std::string> want;
	want.push_back("survival");
	want.push_back("classic");

	const std::vector<AddonRemix> offered = OfferedRemixes(EntryWith(want), -1, Pool());

	ASSERT_EQ(2u, offered.size());
	EXPECT_EQ("survival", offered[0].id);
	EXPECT_EQ("classic", offered[1].id);
}

TEST(OfferedRemixes, ARemixThePoolDoesNotHaveCostsOnlyItself)
{
	// Catalogues are edited by hand and a typo in one id should not take the entry's other ways of
	// playing down with it.
	std::vector<std::string> want;
	want.push_back("classic");
	want.push_back("no-such-remix");
	want.push_back("survival");

	const std::vector<AddonRemix> offered = OfferedRemixes(EntryWith(want), -1, Pool());

	ASSERT_EQ(2u, offered.size());
	EXPECT_EQ("classic", offered[0].id);
	EXPECT_EQ("survival", offered[1].id);
}

TEST(OfferedRemixes, ARemixThatFailedToParseIsNotOffered)
{
	// It is in the pool because the folder was there, and unusable because the JSON was not. Offering
	// it would put a row in the picker that does nothing when chosen.
	std::vector<AddonRemix> pool;
	pool.push_back(Remix("classic", "Classic"));
	pool.push_back(Remix("survival", "Survival", false));

	std::vector<std::string> want;
	want.push_back("classic");
	want.push_back("survival");

	const std::vector<AddonRemix> offered = OfferedRemixes(EntryWith(want), -1, pool);

	ASSERT_EQ(1u, offered.size());
	EXPECT_EQ("classic", offered[0].id);
}

TEST(OfferedRemixes, ARemixNamedTwiceIsOfferedTwice)
{
	// Pinned rather than deduplicated: the loop takes the first pool match per NAMED id, so a
	// catalogue that repeats itself gets a repeated row and the author sees their own mistake.
	std::vector<std::string> want;
	want.push_back("classic");
	want.push_back("classic");

	EXPECT_EQ(2u, OfferedRemixes(EntryWith(want), -1, Pool()).size());
}

// ------------------------------------------------------------ a variant's own list

TEST(OfferedRemixes, AVariantsListOverridesTheEntrys)
{
	// [rc4l] Overriding and not adding, because the case this exists for is one way of playing taking
	// something the others must not: Skulltag's Invasion offers three lives and its Duel cannot.
	std::vector<std::string> entryWants;
	entryWants.push_back("classic");

	AddonEntry entry = EntryWith(entryWants);

	AddonVariant duel;
	duel.remixes.push_back("teamdm");
	duel.remixesSet = true;
	entry.variants.push_back(duel);

	const std::vector<AddonRemix> offered = OfferedRemixes(entry, 0, Pool());

	ASSERT_EQ(1u, offered.size());
	EXPECT_EQ("teamdm", offered[0].id) << "the entry's list must not show through";
}

TEST(OfferedRemixes, AVariantMayWriteAnEmptyListToTakeNone)
{
	// [rc4l] The override is keyed on the KEY being there, not on the list being non-empty, and this
	// is why. Both Doom Barracks Zones sit in an entry offering four mixes and can take none of them:
	// the pack replaces the weapons itself. Read as silence, "remixes": [] handed them all four.
	std::vector<std::string> entryWants;
	entryWants.push_back("classic");
	entryWants.push_back("survival");

	AddonEntry entry = EntryWith(entryWants);

	AddonVariant dbz;
	dbz.remixesSet = true;
	entry.variants.push_back(dbz);

	EXPECT_TRUE(OfferedRemixes(entry, 0, Pool()).empty());
}

TEST(OfferedRemixes, AVariantWithNoListOfItsOwnFallsBackToTheEntrys)
{
	// The common shape: the entry says what it plays with and the variants all agree.
	std::vector<std::string> entryWants;
	entryWants.push_back("classic");
	entryWants.push_back("survival");

	AddonEntry entry = EntryWith(entryWants);
	entry.variants.push_back(AddonVariant());

	EXPECT_EQ(2u, OfferedRemixes(entry, 0, Pool()).size());
}

TEST(OfferedRemixes, AVariantIndexOutOfRangeFallsBackToTheEntrys)
{
	// -1 for "no variant chosen yet", and anything past the end for a remembered index that a
	// catalogue update shortened. Neither is a reason to show an empty picker.
	std::vector<std::string> entryWants;
	entryWants.push_back("classic");

	AddonEntry entry = EntryWith(entryWants);
	entry.variants.push_back(AddonVariant());

	EXPECT_EQ(1u, OfferedRemixes(entry, -1, Pool()).size());
	EXPECT_EQ(1u, OfferedRemixes(entry, 1, Pool()).size());
	EXPECT_EQ(1u, OfferedRemixes(entry, 99, Pool()).size());
}

TEST(OfferedRemixes, AnEmptyPoolOffersNothingHoweverMuchIsNamed)
{
	std::vector<std::string> want;
	want.push_back("classic");
	want.push_back("survival");

	EXPECT_TRUE(OfferedRemixes(EntryWith(want), -1, std::vector<AddonRemix>()).empty());
}

// ------------------------------------------------------------ which one is in force

TEST(PickRemix, NothingOfferedPicksNothing)
{
	// index -1 is what tells the caller to draw no button, so it has to be the answer here rather
	// than a 0 that points at a remix that does not exist.
	const RemixPick pick = PickRemix(std::vector<AddonRemix>(), "classic");

	EXPECT_EQ(-1, pick.index);
	EXPECT_TRUE(pick.id.empty());
	EXPECT_TRUE(pick.name.empty());
	EXPECT_TRUE(pick.cfg.empty());
	EXPECT_TRUE(pick.files.empty());
}

TEST(PickRemix, NoPreferencePicksTheBaseline)
{
	// First offered is the baseline by convention, so somebody who has never chosen gets the pack the
	// way its author meant it.
	const RemixPick pick = PickRemix(Pool(), "");

	EXPECT_EQ(0, pick.index);
	EXPECT_EQ("classic", pick.id);
	EXPECT_EQ("Classic", pick.name);
}

TEST(PickRemix, AChoiceStillOnOfferIsHonoured)
{
	const RemixPick pick = PickRemix(Pool(), "teamdm");

	EXPECT_EQ(2, pick.index);
	EXPECT_EQ("teamdm", pick.id);
	EXPECT_EQ("Team Deathmatch", pick.name);
}

TEST(PickRemix, AWithdrawnChoiceFallsBackToTheBaseline)
{
	// A catalogue update that dropped a remix must not leave the player on it. Falling back to the
	// baseline is the same answer as never having chosen, which is the honest thing to show.
	const RemixPick pick = PickRemix(Pool(), "no-longer-here");

	EXPECT_EQ(0, pick.index);
	EXPECT_EQ("classic", pick.id) << "the pick reports what is IN FORCE, not what was asked for";
}

TEST(PickRemix, TheCfgAndFilesComeFromTheRemixChosen)
{
	// What the caller actually uses: the cfg goes on the command line as a second +exec and the files
	// are appended to the entry's.
	std::vector<AddonRemix> offered;
	offered.push_back(Remix("classic", "Classic"));

	AddonRemix survival = Remix("survival", "Survival");
	survival.cfg = "survival.cfg";
	survival.files.push_back(File("lives.pk3"));
	offered.push_back(survival);

	const RemixPick pick = PickRemix(offered, "survival");

	EXPECT_EQ("survival.cfg", pick.cfg);
	ASSERT_EQ(1u, pick.files.size());
	EXPECT_EQ("lives.pk3", pick.files[0].name);
}

TEST(PickRemix, TheBaselineIsAllowedToAddNothing)
{
	// It usually does add nothing: "Classic" is the pack as it ships, and its whole job is to be a
	// row the player can come back to.
	const RemixPick pick = PickRemix(Pool(), "classic");

	EXPECT_EQ(0, pick.index);
	EXPECT_TRUE(pick.cfg.empty());
	EXPECT_TRUE(pick.files.empty());
}

// ------------------------------------------------------------ axes

TEST(GroupRemixes, EverythingUngroupedIsOneAxis)
{
	// [rc4l] The shape every remix written before groups existed has. It must keep behaving as one
	// flat mutually-exclusive list, or adding the field would silently re-mean every entry.
	const std::vector<RemixGroup> groups = GroupRemixes(Pool());

	ASSERT_EQ(1u, groups.size());
	EXPECT_TRUE(groups[0].id.empty());
	EXPECT_EQ(3u, groups[0].choices.size());
}

TEST(GroupRemixes, NothingOfferedIsNoAxes)
{
	EXPECT_TRUE(GroupRemixes(std::vector<AddonRemix>()).empty());
}

TEST(GroupRemixes, RemixesSplitByGroupAndKeepEntryOrderWithinEach)
{
	std::vector<AddonRemix> offered;
	offered.push_back(Grouped("classic", "Classic", "lives"));
	offered.push_back(Grouped("nomod", "None", "mod"));
	offered.push_back(Grouped("survival", "Survival", "lives"));
	offered.push_back(Grouped("brutal", "Brutal Doom", "mod"));

	const std::vector<RemixGroup> groups = GroupRemixes(offered);

	ASSERT_EQ(2u, groups.size());
	EXPECT_EQ("lives", groups[0].id) << "group order is first appearance, not alphabetical";
	EXPECT_EQ("mod", groups[1].id);

	ASSERT_EQ(2u, groups[0].choices.size());
	EXPECT_EQ("classic", groups[0].choices[0].id);
	EXPECT_EQ("survival", groups[0].choices[1].id) << "interleaved members still collect in order";

	ASSERT_EQ(2u, groups[1].choices.size());
	EXPECT_EQ("nomod", groups[1].choices[0].id);
	EXPECT_EQ("brutal", groups[1].choices[1].id);
}

TEST(GroupRemixes, AGroupOfOneIsStillAGroup)
{
	// Nothing to decide, but that is the caller's problem to draw. Collapsing it here would hide an
	// axis that exists, and the axis is what says a mod is applied at all.
	std::vector<AddonRemix> offered;
	offered.push_back(Grouped("classic", "Classic", "lives"));
	offered.push_back(Grouped("brutal", "Brutal Doom", "mod"));

	const std::vector<RemixGroup> groups = GroupRemixes(offered);

	ASSERT_EQ(2u, groups.size());
	EXPECT_EQ(1u, groups[0].choices.size());
	EXPECT_EQ(1u, groups[1].choices.size());
}

TEST(GroupRemixes, GroupedAndUngroupedCoexist)
{
	// The migration state: some remixes carry a group and the rest have not been touched yet. The
	// untouched ones share the default axis rather than becoming one axis each.
	std::vector<AddonRemix> offered;
	offered.push_back(Remix("ffa", "Free for all"));
	offered.push_back(Grouped("brutal", "Brutal Doom", "mod"));
	offered.push_back(Remix("teamdm", "Team Deathmatch"));

	const std::vector<RemixGroup> groups = GroupRemixes(offered);

	ASSERT_EQ(2u, groups.size());
	EXPECT_TRUE(groups[0].id.empty());
	EXPECT_EQ(2u, groups[0].choices.size());
	EXPECT_EQ("mod", groups[1].id);
}

TEST(PickRemixes, EachAxisResolvesOnItsOwn)
{
	std::vector<AddonRemix> offered;
	offered.push_back(Grouped("classic", "Classic", "lives"));
	offered.push_back(Grouped("survival", "Survival", "lives"));
	offered.push_back(Grouped("nomod", "None", "mod"));
	offered.push_back(Grouped("brutal", "Brutal Doom", "mod"));

	std::vector<std::pair<std::string, std::string> > wanted;
	wanted.push_back(Want("mod", "brutal"));

	const std::vector<RemixPick> picks = PickRemixes(offered, wanted);

	ASSERT_EQ(2u, picks.size());
	EXPECT_EQ("classic", picks[0].id) << "an axis nobody chose on falls to its own baseline";
	EXPECT_EQ("brutal", picks[1].id);
	EXPECT_EQ(1, picks[1].index) << "the index is into the GROUP, not the whole offered list";
}

TEST(PickRemixes, AStalePreferenceCostsOnlyItsOwnAxis)
{
	// [rc4l] The property that makes independent axes worth having. A remix withdrawn from one group
	// must not disturb what is chosen on another, or every catalogue edit becomes a reset.
	std::vector<AddonRemix> offered;
	offered.push_back(Grouped("classic", "Classic", "lives"));
	offered.push_back(Grouped("survival", "Survival", "lives"));
	offered.push_back(Grouped("nomod", "None", "mod"));
	offered.push_back(Grouped("brutal", "Brutal Doom", "mod"));

	std::vector<std::pair<std::string, std::string> > wanted;
	wanted.push_back(Want("lives", "gone-away"));
	wanted.push_back(Want("mod", "brutal"));
	wanted.push_back(Want("no-such-group", "whatever"));

	const std::vector<RemixPick> picks = PickRemixes(offered, wanted);

	ASSERT_EQ(2u, picks.size()) << "a group that does not exist adds nothing";
	EXPECT_EQ("classic", picks[0].id) << "the withdrawn choice fell back";
	EXPECT_EQ("brutal", picks[1].id) << "and the other axis was left alone";
}

TEST(PickRemixes, NothingOfferedPicksNothing)
{
	EXPECT_TRUE(PickRemixes(std::vector<AddonRemix>(),
		std::vector<std::pair<std::string, std::string> >()).empty());
}

TEST(PickRemixes, NoPreferenceAtAllTakesEveryBaseline)
{
	std::vector<AddonRemix> offered;
	offered.push_back(Grouped("classic", "Classic", "lives"));
	offered.push_back(Grouped("survival", "Survival", "lives"));
	offered.push_back(Grouped("nomod", "None", "mod"));
	offered.push_back(Grouped("brutal", "Brutal Doom", "mod"));

	const std::vector<RemixPick> picks =
		PickRemixes(offered, std::vector<std::pair<std::string, std::string> >());

	ASSERT_EQ(2u, picks.size());
	EXPECT_EQ("classic", picks[0].id);
	EXPECT_EQ("nomod", picks[1].id);
	EXPECT_EQ(0, picks[0].index);
	EXPECT_EQ(0, picks[1].index);
}

TEST(PickRemix, EveryOfferedRemixIsReachable)
{
	// Swept because the picker draws a row per offered remix, and a row that cannot be selected is
	// worse than one that is not drawn.
	const std::vector<AddonRemix> offered = Pool();

	for (size_t i = 0; i < offered.size(); ++i)
	{
		const RemixPick pick = PickRemix(offered, offered[i].id);

		EXPECT_EQ(static_cast<int>(i), pick.index) << "id=" << offered[i].id;
		EXPECT_EQ(offered[i].id, pick.id);
	}
}

namespace
{

AddonFileRef Filling(const std::string &name, const std::string &role)
{
	AddonFileRef f = File(name);
	f.provides = role;
	return f;
}

// A pick as PickRemix would have built it: what it loads, and what it says it already contains.
RemixPick Mix(const std::string &id, const char *loads, const char *provides)
{
	RemixPick pick;
	pick.index = 0;
	pick.id = id;
	if (loads != NULL)
		pick.files.push_back(File(loads));
	if (provides != NULL)
		pick.provides.push_back(provides);
	return pick;
}

std::vector<std::string> NamesOf(const std::vector<AddonFileRef> &files)
{
	std::vector<std::string> names;
	for (size_t i = 0; i < files.size(); ++i)
		names.push_back(files[i].name);
	return names;
}

// What a deathmatch entry loads: its maps, and a spree announcer it names as one.
std::vector<AddonFileRef> Deathmatch()
{
	std::vector<AddonFileRef> base;
	base.push_back(File("maps.wad"));
	base.push_back(Filling("spree2rc2.pk3", "spree"));
	return base;
}

} // namespace

TEST(CombineFiles, PutsTheMixAfterWhatTheEntryLoads)
{
	const std::vector<RemixPick> picks(1, Mix("weapons", "weapons.pk3", NULL));
	const std::vector<std::string> got = NamesOf(CombineFiles(Deathmatch(), picks));

	ASSERT_EQ(3u, got.size());
	EXPECT_EQ("maps.wad", got[0]);
	EXPECT_EQ("spree2rc2.pk3", got[1]);
	EXPECT_EQ("weapons.pk3", got[2]);
}

TEST(CombineFiles, DropsTheFileWhoseRoleTheMixAlreadyFills)
{
	// The case this exists for: a weapon mod carrying its own spree, offered by an entry that loads
	// one anyway. Both loaded means two trackers over the same kills.
	const std::vector<RemixPick> picks(1, Mix("weapons", "weapons.pk3", "spree"));
	const std::vector<std::string> got = NamesOf(CombineFiles(Deathmatch(), picks));

	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("maps.wad", got[0]);
	EXPECT_EQ("weapons.pk3", got[1]);
}

TEST(CombineFiles, SurvivesTheFileBeingUpgraded)
{
	// THE reason this is a role and not a filename. The entry moves to the next release; nothing
	// about the mix changes; the suppression still fires.
	std::vector<AddonFileRef> next;
	next.push_back(File("maps.wad"));
	next.push_back(Filling("spree3final.pk3", "spree"));

	const std::vector<RemixPick> picks(1, Mix("weapons", "weapons.pk3", "spree"));

	EXPECT_EQ(2u, CombineFiles(next, picks).size());
}

TEST(CombineFiles, LeavesAnUntaggedFileAlone)
{
	// An entry that never said what its file was keeps it. Silence is not a claim to any role, so a
	// mix providing one cannot reach it.
	std::vector<AddonFileRef> base;
	base.push_back(File("maps.wad"));
	base.push_back(File("spree2rc2.pk3"));

	const std::vector<RemixPick> picks(1, Mix("weapons", "weapons.pk3", "spree"));

	EXPECT_EQ(3u, CombineFiles(base, picks).size());
}

TEST(CombineFiles, LeavesADifferentRoleAlone)
{
	std::vector<AddonFileRef> base;
	base.push_back(Filling("hud.pk3", "hud"));

	const std::vector<RemixPick> picks(1, Mix("weapons", NULL, "spree"));

	EXPECT_EQ(1u, CombineFiles(base, picks).size());
}

TEST(CombineFiles, LetsAnyAxisFillTheRole)
{
	// Two independent axes, and it is the SECOND that brings the spree. Nothing about the
	// suppression may depend on which group the mix sits in.
	std::vector<RemixPick> picks;
	picks.push_back(Mix("lives", NULL, NULL));
	picks.push_back(Mix("weapons", "weapons.pk3", "spree"));

	EXPECT_EQ(2u, CombineFiles(Deathmatch(), picks).size());
}

TEST(CombineFiles, KeepsTheMixsOwnCopyOfWhatItProvides)
{
	// A mix may tag its own file with the role it fills. Suppressing that would delete the thing
	// the declaration is about and leave the role filled by nothing.
	RemixPick pick = Mix("weapons", NULL, "spree");
	pick.files.push_back(Filling("weapons.pk3", "spree"));

	const std::vector<std::string> got = NamesOf(CombineFiles(Deathmatch(), std::vector<RemixPick>(1, pick)));

	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("maps.wad", got[0]);
	EXPECT_EQ("weapons.pk3", got[1]);
}

TEST(CombineFiles, OneMixCanReplaceWhatAnotherBrought)
{
	// Across axes, a mix's file is no different from the entry's: whoever provides the role wins and
	// the other copy goes.
	RemixPick brings = Mix("sounds", NULL, NULL);
	brings.files.push_back(Filling("othersp.pk3", "spree"));

	std::vector<RemixPick> picks;
	picks.push_back(brings);
	picks.push_back(Mix("weapons", "weapons.pk3", "spree"));

	std::vector<AddonFileRef> base(1, File("maps.wad"));
	const std::vector<std::string> got = NamesOf(CombineFiles(base, picks));

	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("maps.wad", got[0]);
	EXPECT_EQ("weapons.pk3", got[1]);
}

TEST(CombineFiles, ChangesNothingWhenNoMixIsInForce)
{
	// An entry with no mixes at all comes through here and must load exactly what it named.
	EXPECT_EQ(2u, CombineFiles(Deathmatch(), std::vector<RemixPick>()).size());
}

TEST(PickRemix, CarriesTheProvidedRolesThrough)
{
	// The suppression is useless unless the pick hands the roles on; nothing else does. Both routes
	// through PickRemix are checked: the remembered choice, and the fallback to the baseline.
	AddonRemix mod = Remix("mod", "Weapons");
	mod.provides.push_back("spree");

	const std::vector<AddonRemix> offered(1, mod);

	EXPECT_EQ(mod.provides, PickRemix(offered, "mod").provides);
	EXPECT_EQ(mod.provides, PickRemix(offered, "gone").provides);
}

// ---------------------------------------------------------------- modes on an axis

TEST(RemixPick, APickCarriesTheModeItSwitchesTo)
{
	// Both ways in, because a pick that fell back to the baseline must answer the same as one asked
	// for by name.
	std::vector<AddonRemix> offered;
	offered.push_back(Mode("dm", "Deathmatch", zx::HostGameMode::Deathmatch));
	offered.push_back(Mode("ctf", "Capture the Flag", zx::HostGameMode::CaptureTheFlag));

	EXPECT_EQ(zx::HostGameMode::CaptureTheFlag, PickRemix(offered, "ctf").gameMode);
	EXPECT_EQ(zx::HostGameMode::Deathmatch, PickRemix(offered, "").gameMode);
	EXPECT_EQ(zx::HostGameMode::Deathmatch, PickRemix(offered, "withdrawn").gameMode);
}

TEST(EffectiveGameMode, NothingPickedLeavesWhatTheEntrySaid)
{
	EXPECT_EQ(zx::HostGameMode::Deathmatch,
		zx::EffectiveGameMode(zx::HostGameMode::Deathmatch, std::vector<RemixPick>()));
}

TEST(EffectiveGameMode, AModeMixWinsOverTheEntry)
{
	// The pill is the more recent and more specific statement, the entry saying only how it plays
	// by default.
	std::vector<AddonRemix> offered;
	offered.push_back(Mode("dm", "Deathmatch", zx::HostGameMode::Deathmatch));
	offered.push_back(Mode("ctf", "Capture the Flag", zx::HostGameMode::CaptureTheFlag));

	std::vector<RemixPick> picks(1, PickRemix(offered, "ctf"));

	EXPECT_EQ(zx::HostGameMode::CaptureTheFlag,
		zx::EffectiveGameMode(zx::HostGameMode::Deathmatch, picks));
}

TEST(EffectiveGameMode, AMixThatIsNotAboutTheModeLeavesItAlone)
{
	// Every gameplay mod ever written for this catalogue, none of which may blank the mode simply
	// by being picked.
	std::vector<AddonRemix> offered(1, Remix("brutal", "Brutal Doom"));
	std::vector<RemixPick> picks(1, PickRemix(offered, "brutal"));

	EXPECT_EQ(zx::HostGameMode::Unknown, picks[0].gameMode);
	EXPECT_EQ(zx::HostGameMode::LastManStanding,
		zx::EffectiveGameMode(zx::HostGameMode::LastManStanding, picks));
}

TEST(EffectiveGameMode, TheLastMixNamingAModeIsTheOneInForce)
{
	// Two axes both naming a mode is a catalogue asking for what no server can do, resolved the
	// same way every time so the panel and the server cannot disagree about which of them won.
	std::vector<AddonRemix> offered;
	offered.push_back(Mode("ctf", "Capture the Flag", zx::HostGameMode::CaptureTheFlag));
	offered.push_back(Remix("mod", "Some Mod"));
	offered.push_back(Mode("lms", "Last Man Standing", zx::HostGameMode::LastManStanding));

	std::vector<RemixPick> picks;
	picks.push_back(PickRemix(offered, "ctf"));
	picks.push_back(PickRemix(offered, "mod"));
	picks.push_back(PickRemix(offered, "lms"));

	EXPECT_EQ(zx::HostGameMode::LastManStanding,
		zx::EffectiveGameMode(zx::HostGameMode::Deathmatch, picks));
}
