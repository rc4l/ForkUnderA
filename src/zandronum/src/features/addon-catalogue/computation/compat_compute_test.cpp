// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/compat_compute.h"

#include <string>
#include <vector>

using zx::ActorStyle;
using zx::ActorStyleFromName;
using zx::Addon;
using zx::AddonSlot;
using zx::CheckSelection;
using zx::LoadOrder;
using zx::NameForSlot;
using zx::SlotFromName;
using zx::Verdict;

namespace
{

// The real mods this model was designed against, so a rule change is read against names someone
// recognises rather than against addon_a and addon_b.
Addon Maps(const char *id, ActorStyle actors)
{
	Addon a;
	a.id = id;
	a.fills.push_back(AddonSlot::Maps);
	a.actors = actors;
	return a;
}

Addon Gameplay(const char *id)
{
	Addon a;
	a.id = id;
	a.fills.push_back(AddonSlot::Gameplay);
	return a;
}

Addon Cosmetic(const char *id)
{
	Addon a;
	a.id = id;
	a.fills.push_back(AddonSlot::Cosmetic);
	return a;
}

// Skulltag: fills both slots, tolerates company.
Addon TotalConversion(const char *id)
{
	Addon a;
	a.id = id;
	a.fills.push_back(AddonSlot::Gameplay);
	a.fills.push_back(AddonSlot::Maps);
	return a;
}

// Stronghold, All Out War: fills both and admits nothing else.
Addon StrictStandalone(const char *id)
{
	Addon a = TotalConversion(id);
	a.locks.push_back(AddonSlot::Gameplay);
	a.locks.push_back(AddonSlot::Maps);
	return a;
}

bool Mentions(const zx::CompatResult &r, const char *needle)
{
	for (size_t i = 0; i < r.reasons.size(); ++i)
	{
		if (r.reasons[i].find(needle) != std::string::npos)
			return true;
	}
	return false;
}

} // namespace

// ---------------------------------------------------------------- the case that motivated this

TEST(AddonCompat, ADuelMapPackUnderAWeaponsModIsAllowed)
{
	// duel40 is balanced around vanilla weapons and Brutal Doom replaces all of them. That is a bad
	// idea and a legal load, and the difference matters: this answers "will it run", not "is it
	// wise". A validator that editorialises about taste is not believed about correctness either.
	std::vector<Addon> sel;
	sel.push_back(Maps("duel40", ActorStyle::Vanilla));
	sel.push_back(Gameplay("brutal-doom"));

	const zx::CompatResult r = CheckSelection(sel);
	EXPECT_EQ(Verdict::Allowed, r.verdict);
	EXPECT_TRUE(r.reasons.empty());
}

TEST(AddonCompat, AnUntriedCombinationIsSilentRatherThanFlagged)
{
	// Nobody authored this pair. Silence is the correct answer, not a gap to be filled: the model
	// scales precisely because it does not need an opinion on every pair.
	std::vector<Addon> sel;
	sel.push_back(Maps("scythe", ActorStyle::Vanilla));
	sel.push_back(Gameplay("complex-doom"));
	sel.push_back(Cosmetic("some-hud"));

	EXPECT_EQ(Verdict::Allowed, CheckSelection(sel).verdict);
}

// ---------------------------------------------------------------- blocked

TEST(AddonCompat, TwoGameplayModsAreRefused)
{
	// Both replace the same actors, so loading them is last-one-wins and silently broken. A refusal
	// is kinder than a server nobody can explain.
	std::vector<Addon> sel;
	sel.push_back(Gameplay("brutal-doom"));
	sel.push_back(Gameplay("complex-doom"));

	const zx::CompatResult r = CheckSelection(sel);
	EXPECT_EQ(Verdict::Blocked, r.verdict);
	EXPECT_TRUE(Mentions(r, "gameplay"));
}

TEST(AddonCompat, TwoIwadsAreRefused)
{
	Addon a; a.id = "doom2";    a.fills.push_back(AddonSlot::Iwad);
	Addon b; b.id = "freedoom2"; b.fills.push_back(AddonSlot::Iwad);

	std::vector<Addon> sel;
	sel.push_back(a);
	sel.push_back(b);

	EXPECT_EQ(Verdict::Blocked, CheckSelection(sel).verdict);
}

TEST(AddonCompat, TwoMapPacksAreFineBecauseAMapFixIsNormal)
{
	std::vector<Addon> sel;
	sel.push_back(Maps("alien-vendetta", ActorStyle::Vanilla));
	sel.push_back(Maps("alien-vendetta-fix", ActorStyle::Vanilla));

	EXPECT_EQ(Verdict::Allowed, CheckSelection(sel).verdict);
}

TEST(AddonCompat, AStrictStandaloneRefusesCompany)
{
	std::vector<Addon> sel;
	sel.push_back(StrictStandalone("stronghold"));
	sel.push_back(Maps("scythe", ActorStyle::Vanilla));

	const zx::CompatResult r = CheckSelection(sel);
	EXPECT_EQ(Verdict::Blocked, r.verdict);
	EXPECT_TRUE(Mentions(r, "stronghold"));
}

TEST(AddonCompat, AStrictStandaloneOnItsOwnIsFine)
{
	// It fills the very slots it locks. If a lock counted against its owner, nothing that locks
	// anything could ever be selected, which would make the feature useless.
	std::vector<Addon> sel;
	sel.push_back(StrictStandalone("all-out-war"));

	EXPECT_EQ(Verdict::Allowed, CheckSelection(sel).verdict);
}

TEST(AddonCompat, AStrictStandaloneStillAdmitsAnUnlockedSlot)
{
	// Stronghold locks gameplay and maps but says nothing about cosmetic, so a HUD is still legal.
	std::vector<Addon> sel;
	sel.push_back(StrictStandalone("stronghold"));
	sel.push_back(Cosmetic("widescreen-hud"));

	EXPECT_EQ(Verdict::Allowed, CheckSelection(sel).verdict);
}

TEST(AddonCompat, ATotalConversionThatDoesNotLockTakesCompany)
{
	// Skulltag fills both slots without locking them, so Skulltag plus a mappack is legal. This is
	// the line between "standalone" and "strict standalone".
	std::vector<Addon> sel;
	sel.push_back(TotalConversion("skulltag"));
	sel.push_back(Maps("scythe", ActorStyle::Vanilla));

	EXPECT_EQ(Verdict::Allowed, CheckSelection(sel).verdict);
}

TEST(AddonCompat, ATotalConversionStillCollidesWithAGameplayMod)
{
	// It fills gameplay, so the arity rule catches it with no extra logic.
	std::vector<Addon> sel;
	sel.push_back(TotalConversion("skulltag"));
	sel.push_back(Gameplay("brutal-doom"));

	EXPECT_EQ(Verdict::Blocked, CheckSelection(sel).verdict);
}

// ---------------------------------------------------------------- warned

TEST(AddonCompat, ADeclaredConflictWarnsButDoesNotBlock)
{
	Addon brutal = Gameplay("brutal-doom");
	brutal.conflictsWith.push_back("some-hud");

	std::vector<Addon> sel;
	sel.push_back(brutal);
	sel.push_back(Cosmetic("some-hud"));

	const zx::CompatResult r = CheckSelection(sel);
	EXPECT_EQ(Verdict::Warned, r.verdict);
	EXPECT_TRUE(Mentions(r, "declared"));
}

TEST(AddonCompat, EitherSideOfAConflictIsEnoughToDeclareIt)
{
	// Only one of a pair should have to know, or every conflict needs two edits and one will be
	// forgotten.
	Addon hud = Cosmetic("some-hud");
	hud.conflictsWith.push_back("brutal-doom");

	std::vector<Addon> sel;
	sel.push_back(Gameplay("brutal-doom"));
	sel.push_back(hud);

	EXPECT_EQ(Verdict::Warned, CheckSelection(sel).verdict);
}

TEST(AddonCompat, CustomActorMapsUnderAGameplayModWarn)
{
	std::vector<Addon> sel;
	sel.push_back(Maps("mappack-with-monsters", ActorStyle::Custom));
	sel.push_back(Gameplay("brutal-doom"));

	const zx::CompatResult r = CheckSelection(sel);
	EXPECT_EQ(Verdict::Warned, r.verdict);
	EXPECT_TRUE(Mentions(r, "own actors"));
}

TEST(AddonCompat, AnUndeclaredActorStyleIsTreatedAsCustom)
{
	// Guessing wrong the safe way costs a warning; guessing wrong the other way costs a broken
	// server the host cannot diagnose.
	std::vector<Addon> sel;
	sel.push_back(Maps("unknown-pack", ActorStyle::Unknown));
	sel.push_back(Gameplay("brutal-doom"));

	EXPECT_EQ(Verdict::Warned, CheckSelection(sel).verdict);
}

TEST(AddonCompat, CustomActorMapsAloneAreFine)
{
	// Nothing to fight with.
	std::vector<Addon> sel;
	sel.push_back(Maps("mappack-with-monsters", ActorStyle::Custom));

	EXPECT_EQ(Verdict::Allowed, CheckSelection(sel).verdict);
}

TEST(AddonCompat, ATotalConversionDoesNotWarnAboutItsOwnMaps)
{
	// It fills gameplay AND maps, so a naive check would have it warn about itself forever.
	std::vector<Addon> sel;
	sel.push_back(TotalConversion("skulltag"));

	EXPECT_EQ(Verdict::Allowed, CheckSelection(sel).verdict);
}

TEST(AddonCompat, BlockedOutranksWarned)
{
	Addon brutal = Gameplay("brutal-doom");
	brutal.conflictsWith.push_back("complex-doom");

	std::vector<Addon> sel;
	sel.push_back(brutal);
	sel.push_back(Gameplay("complex-doom"));		// also an arity violation

	const zx::CompatResult r = CheckSelection(sel);
	EXPECT_EQ(Verdict::Blocked, r.verdict);
	EXPECT_GE(r.reasons.size(), 2u) << "every finding should be reported, not just the first";
}

TEST(AddonCompat, AnEmptySelectionIsAllowed)
{
	EXPECT_EQ(Verdict::Allowed, CheckSelection(std::vector<Addon>()).verdict);
}

// ---------------------------------------------------------------- load order

TEST(AddonCompat, LoadOrderFollowsTheSlotAndNotTheSelection)
{
	// Picked backwards on purpose. A gameplay mod's actors must land after the mappack it is layered
	// on, and taking that out of the user's hands is half the point of the feature.
	std::vector<Addon> sel;
	sel.push_back(Cosmetic("hud"));
	sel.push_back(Gameplay("brutal-doom"));
	sel.push_back(Maps("scythe", ActorStyle::Vanilla));

	const std::vector<std::string> order = LoadOrder(sel);
	ASSERT_EQ(3u, order.size());
	EXPECT_EQ("scythe", order[0]);
	EXPECT_EQ("brutal-doom", order[1]);
	EXPECT_EQ("hud", order[2]);
}

TEST(AddonCompat, AnAddonFillingSeveralSlotsAppearsOnce)
{
	std::vector<Addon> sel;
	sel.push_back(TotalConversion("skulltag"));
	sel.push_back(Cosmetic("hud"));

	const std::vector<std::string> order = LoadOrder(sel);
	ASSERT_EQ(2u, order.size());
	EXPECT_EQ("skulltag", order[0]);
	EXPECT_EQ("hud", order[1]);
}

TEST(AddonCompat, TwoInTheSameSlotKeepTheirSelectionOrder)
{
	// The slot rule is not negotiable; ties inside a slot stay the caller's business, which is how a
	// mappack and its fix end up the right way round.
	std::vector<Addon> sel;
	sel.push_back(Maps("alien-vendetta", ActorStyle::Vanilla));
	sel.push_back(Maps("alien-vendetta-fix", ActorStyle::Vanilla));

	const std::vector<std::string> order = LoadOrder(sel);
	ASSERT_EQ(2u, order.size());
	EXPECT_EQ("alien-vendetta", order[0]);
	EXPECT_EQ("alien-vendetta-fix", order[1]);
}

TEST(AddonCompat, LoadOrderOfNothingIsNothing)
{
	EXPECT_TRUE(LoadOrder(std::vector<Addon>()).empty());
}

// ---------------------------------------------------------------- names, which ship on disk

TEST(AddonCompat, EverySlotRoundTripsThroughItsName)
{
	// The catalogue stores names, never ordinals, so a renumbering cannot change what an entry
	// already on a player's disk means.
	const AddonSlot all[] = { AddonSlot::Iwad, AddonSlot::Maps, AddonSlot::Gameplay,
		AddonSlot::Patch, AddonSlot::Cosmetic };

	for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
	{
		AddonSlot back;
		ASSERT_TRUE(SlotFromName(NameForSlot(all[i]), back)) << NameForSlot(all[i]);
		EXPECT_EQ(all[i], back);
	}
}

TEST(AddonCompat, AnUnknownSlotNameIsRefusedRatherThanGuessed)
{
	// An entry written for a newer schema must be skipped, not misread as something it is not.
	AddonSlot out = AddonSlot::Iwad;
	EXPECT_FALSE(SlotFromName("weapons", out));
	EXPECT_FALSE(SlotFromName("", out));
	EXPECT_FALSE(SlotFromName(0, out));
}

TEST(AddonCompat, NamingASlotThatIsNotOneYieldsEmpty)
{
	EXPECT_STREQ("", NameForSlot(AddonSlot::Count));
}

TEST(AddonCompat, ActorStyleParsesByNameAndRefusesTheRest)
{
	ActorStyle out = ActorStyle::Unknown;

	ASSERT_TRUE(ActorStyleFromName("vanilla", out));
	EXPECT_EQ(ActorStyle::Vanilla, out);

	ASSERT_TRUE(ActorStyleFromName("custom", out));
	EXPECT_EQ(ActorStyle::Custom, out);

	EXPECT_FALSE(ActorStyleFromName("mixed", out));
	EXPECT_FALSE(ActorStyleFromName(0, out));
}
