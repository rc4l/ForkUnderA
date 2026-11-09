// [Doruk-Aksoy] Tests for controlfilter_compute: wildcard and substring matching, and which rows of a sectioned key list a search keeps.
#include <gtest/gtest.h>

#include <cstddef>

#include "features/controls-search/computation/controlfilter_compute.h"

using zx::ComputeControlFilter;
using zx::ComputeControlPatternMatch;
using zx::ControlRow;
using zx::ControlRowKind;

namespace
{

// [Doruk-Aksoy] Shaped like the stock menu followed by a mod's KEYCONF section.
std::vector<ControlRow> Menu()
{
	std::vector<ControlRow> rows;
	rows.push_back(ControlRow(ControlRowKind::Heading, "Controls"));               // 0
	rows.push_back(ControlRow(ControlRowKind::Item, "Fire", "+attack"));            // 1
	rows.push_back(ControlRow(ControlRowKind::Item, "Use / Open", "+use"));         // 2
	rows.push_back(ControlRow(ControlRowKind::Spacer, " "));                         // 3
	rows.push_back(ControlRow(ControlRowKind::Heading, "Weapons"));                 // 4
	rows.push_back(ControlRow(ControlRowKind::Item, "Next weapon", "weapnext"));    // 5
	rows.push_back(ControlRow(ControlRowKind::Item, "Previous weapon", "weapprev"));// 6
	rows.push_back(ControlRow(ControlRowKind::Spacer, " "));                         // 7
	rows.push_back(ControlRow(ControlRowKind::Heading, "Death and Decay"));         // 8
	rows.push_back(ControlRow(ControlRowKind::Item, "DnD Menu", "+dnd_menu"));      // 9
	rows.push_back(ControlRow(ControlRowKind::Item, "Drop Ammo", "dnd_dropammo")); // 10
	rows.push_back(ControlRow(ControlRowKind::Item, "Quick DnD Fire", "+dnd_quick"));// 11
	return rows;
}

std::vector<int> Ids(std::initializer_list<int> list)
{
	return std::vector<int>(list);
}

TEST(ControlRow, ADefaultRowIsAnUntitledItem)
{
	const ControlRow row;
	EXPECT_EQ(ControlRowKind::Item, row.kind);
	EXPECT_TRUE(row.label.empty());
	EXPECT_TRUE(row.command.empty());
}

TEST(ControlPatternMatch, EmptyOrBlankMatchesEverything)
{
	EXPECT_TRUE(ComputeControlPatternMatch("", "Fire"));
	EXPECT_TRUE(ComputeControlPatternMatch("   ", ""));
}

TEST(ControlPatternMatch, PlainTextIsACaseInsensitiveSubstring)
{
	EXPECT_TRUE(ComputeControlPatternMatch("weap", "Next Weapon"));
	EXPECT_TRUE(ComputeControlPatternMatch(" FIRE ", "Quick DnD fire"));
	EXPECT_FALSE(ComputeControlPatternMatch("jump", "Fire"));
}

TEST(ControlPatternMatch, TrailingStarAnchorsTheStart)
{
	EXPECT_TRUE(ComputeControlPatternMatch("DnD*", "DnD Menu"));
	EXPECT_TRUE(ComputeControlPatternMatch("dnd*", "DND"));
	EXPECT_FALSE(ComputeControlPatternMatch("DnD*", "Quick DnD Fire"));
}

TEST(ControlPatternMatch, LeadingStarAnchorsTheEnd)
{
	EXPECT_TRUE(ComputeControlPatternMatch("*weapon", "Next weapon"));
	EXPECT_FALSE(ComputeControlPatternMatch("*weapon", "Weapon zoom"));
}

TEST(ControlPatternMatch, StarsInTheMiddleBacktrack)
{
	EXPECT_TRUE(ComputeControlPatternMatch("q*d*fire", "Quick DnD Fire"));
	EXPECT_TRUE(ComputeControlPatternMatch("a*a*b", "aaab"));
	EXPECT_FALSE(ComputeControlPatternMatch("q*z*", "Quick DnD Fire"));
	EXPECT_TRUE(ComputeControlPatternMatch("**", ""));
}

TEST(ControlPatternMatch, QuestionMarkIsOneCharacter)
{
	EXPECT_TRUE(ComputeControlPatternMatch("f?re", "Fire"));
	EXPECT_FALSE(ComputeControlPatternMatch("f?re", "Fre"));
	EXPECT_FALSE(ComputeControlPatternMatch("fire?", "Fire"));
}

TEST(ControlFilter, EmptyPatternKeepsEveryRow)
{
	const std::vector<int> all = ComputeControlFilter(Menu(), " ");
	ASSERT_EQ(all.size(), Menu().size());
	for (size_t i = 0; i < all.size(); ++i)
		EXPECT_EQ(all[i], static_cast<int>(i));
}

TEST(ControlFilter, MatchingItemsKeepTheirHeadingAndASpacerBetweenSections)
{
	EXPECT_EQ(Ids({0, 1, 7, 8, 11}), ComputeControlFilter(Menu(), "fire"));
}

TEST(ControlFilter, AFirstSectionNeverStartsWithABlankLine)
{
	EXPECT_EQ(Ids({4, 5}), ComputeControlFilter(Menu(), "next"));
}

TEST(ControlFilter, CommandsMatchToo)
{
	EXPECT_EQ(Ids({8, 10}), ComputeControlFilter(Menu(), "dropammo"));
}

TEST(ControlFilter, AMatchingHeadingKeepsItsWholeSection)
{
	EXPECT_EQ(Ids({8, 9, 10, 11}), ComputeControlFilter(Menu(), "decay"));
}

TEST(ControlFilter, PrefixPatternMatchesLabelsOrCommandsStartingWithIt)
{
	EXPECT_EQ(Ids({8, 9, 10}), ComputeControlFilter(Menu(), "DnD*"));
}

TEST(ControlFilter, NoMatchKeepsNothing)
{
	EXPECT_TRUE(ComputeControlFilter(Menu(), "zzz").empty());
}

TEST(ControlFilter, ItemsBeforeAnyHeadingAndEmptySectionsAreHandled)
{
	std::vector<ControlRow> rows;
	rows.push_back(ControlRow(ControlRowKind::Item, "Loose fire", "+attack"));  // 0
	rows.push_back(ControlRow(ControlRowKind::Heading, "Empty"));               // 1
	rows.push_back(ControlRow(ControlRowKind::Spacer, ""));                      // 2
	rows.push_back(ControlRow(ControlRowKind::Item, "Untitled fire", "+alt"));  // 3

	EXPECT_EQ(Ids({0, 3}), ComputeControlFilter(rows, "fire"));
}

} // namespace
