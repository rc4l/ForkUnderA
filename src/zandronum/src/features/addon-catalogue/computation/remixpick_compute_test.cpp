// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/remixpick_compute.h"

using zx::AddonEntry;
using zx::AddonFileRef;
using zx::AddonRemix;
using zx::AddonVariant;
using zx::OfferedRemixes;
using zx::PickRemix;
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
	entry.variants.push_back(duel);

	const std::vector<AddonRemix> offered = OfferedRemixes(entry, 0, Pool());

	ASSERT_EQ(1u, offered.size());
	EXPECT_EQ("teamdm", offered[0].id) << "the entry's list must not show through";
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
