// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/pickerview_compute.h"

#include <string>
#include <vector>

using zx::BuildPickerView;
using zx::PickerItem;
using zx::PickerView;

namespace
{

PickerItem Item(const char *id, const char *name)
{
	PickerItem i;
	i.id = id;
	i.name = name;
	return i;
}

std::vector<PickerItem> Catalogue()
{
	std::vector<PickerItem> v;
	v.push_back(Item("duel40", "Duel 40"));
	v.push_back(Item("skulltag", "Skulltag"));
	v.push_back(Item("brutal-doom", "Brutal Doom"));
	return v;
}

} // namespace

// ---------------------------------------------------------------- the reason this exists

TEST(PickerView, TypingKeepsYouOnTheSameEntryNotTheSameRow)
{
	// Selected Skulltag at row 1, then typed. Skulltag is now row 0 of the filtered list. Following
	// the row rather than the entry would leave the highlight on whatever moved into position 1, and
	// the player starts a different server than the one they were reading about.
	const std::vector<PickerItem> items = Catalogue();

	const PickerView before = BuildPickerView(items, "", "skulltag");
	EXPECT_EQ(1, before.selectedRow);

	const PickerView after = BuildPickerView(items, "s", "skulltag");
	EXPECT_EQ("skulltag", after.selectedId);
	EXPECT_EQ(0, after.selectedRow) << "same entry, different row";
}

TEST(PickerView, AnEntryThatFiltersOutHandsSelectionToTheFirstVisibleRow)
{
	// Leaving nothing selected would blank the panel beside the list for no reason a player could
	// explain, so the highlight lands on what they are actually looking at.
	const PickerView v = BuildPickerView(Catalogue(), "brutal", "skulltag");

	ASSERT_EQ(1u, v.visible.size());
	EXPECT_EQ(0, v.selectedRow);
	EXPECT_EQ("brutal-doom", v.selectedId);
}

TEST(PickerView, MatchingNothingSelectsNothing)
{
	const PickerView v = BuildPickerView(Catalogue(), "no such thing", "skulltag");

	EXPECT_TRUE(v.visible.empty());
	EXPECT_EQ(-1, v.selectedRow);
	EXPECT_TRUE(v.selectedId.empty());
}

TEST(PickerView, AFreshPickerIsNeverSittingOnNothing)
{
	// No previous selection, so the first row is chosen rather than leaving the detail panel empty
	// until the player happens to click something.
	const PickerView v = BuildPickerView(Catalogue(), "", "");

	EXPECT_EQ(0, v.selectedRow);
	EXPECT_EQ("duel40", v.selectedId);
}

// ---------------------------------------------------------------- filtering

TEST(PickerView, AnEmptyQueryIsTheAbsenceOfAFilter)
{
	const PickerView v = BuildPickerView(Catalogue(), "", "");
	EXPECT_EQ(3u, v.visible.size());
}

TEST(PickerView, SearchIsCaseInsensitiveAndMatchesAnywhere)
{
	EXPECT_EQ(1u, BuildPickerView(Catalogue(), "SKULL", "").visible.size());
	EXPECT_EQ(1u, BuildPickerView(Catalogue(), "doom", "").visible.size());
	EXPECT_EQ(1u, BuildPickerView(Catalogue(), " 40", "").visible.size());
}

TEST(PickerView, VisibleRowsKeepTheCatalogueOrder)
{
	// The list is not re-sorted by relevance. Whatever order the caller supplied is the order shown,
	// so a player who has learned where something sits keeps finding it there.
	const PickerView v = BuildPickerView(Catalogue(), "", "");

	ASSERT_EQ(3u, v.visible.size());
	EXPECT_EQ(0u, v.visible[0]);
	EXPECT_EQ(1u, v.visible[1]);
	EXPECT_EQ(2u, v.visible[2]);
}

TEST(PickerView, VisibleIndexesPointAtTheCallersItems)
{
	// The caller looks the entry up by this index, so an off-by-one here shows the wrong detail panel
	// for the right row.
	const std::vector<PickerItem> items = Catalogue();
	const PickerView v = BuildPickerView(items, "brutal", "");

	ASSERT_EQ(1u, v.visible.size());
	EXPECT_EQ(2u, v.visible[0]);
	EXPECT_EQ("Brutal Doom", items[v.visible[0]].name);
}

// ---------------------------------------------------------------- edges

TEST(PickerView, AnEmptyCatalogueSelectsNothingRatherThanCrashing)
{
	const PickerView v = BuildPickerView(std::vector<PickerItem>(), "", "anything");

	EXPECT_TRUE(v.visible.empty());
	EXPECT_EQ(-1, v.selectedRow);
}

TEST(PickerView, AKeepIdThatWasNeverInTheListIsIgnored)
{
	const PickerView v = BuildPickerView(Catalogue(), "", "deleted-yesterday");

	EXPECT_EQ(0, v.selectedRow);
	EXPECT_EQ("duel40", v.selectedId);
}

TEST(PickerView, TwoEntriesSharingANameStillSelectByIdentity)
{
	// Names are not unique; ids are. Selecting the second of two "Duel 40" entries must survive a
	// keystroke rather than snapping to the first.
	std::vector<PickerItem> items;
	items.push_back(Item("duel40-a", "Duel 40"));
	items.push_back(Item("duel40-b", "Duel 40"));

	const PickerView v = BuildPickerView(items, "duel", "duel40-b");

	EXPECT_EQ(1, v.selectedRow);
	EXPECT_EQ("duel40-b", v.selectedId);
}

TEST(PickerView, ClearingTheSearchRestoresTheFullListAndKeepsTheSelection)
{
	// The round trip a player actually does: type, find, then clear the box.
	const std::vector<PickerItem> items = Catalogue();

	const PickerView filtered = BuildPickerView(items, "brutal", "");
	ASSERT_EQ("brutal-doom", filtered.selectedId);

	const PickerView cleared = BuildPickerView(items, "", filtered.selectedId);
	EXPECT_EQ(3u, cleared.visible.size());
	EXPECT_EQ("brutal-doom", cleared.selectedId);
	EXPECT_EQ(2, cleared.selectedRow);
}
