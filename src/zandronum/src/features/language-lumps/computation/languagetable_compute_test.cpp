#include "languagetable_compute.h"

#include <gtest/gtest.h>
#include <initializer_list>
#include <string>
#include <utility>

using namespace zx::language;

namespace {

LanguageGenderStrings One(const char *text)
{
	return LanguageGenderStrings(1, text);
}

LanguageTable Aliases(std::initializer_list<std::pair<const char *, const char *>> entries)
{
	LanguageTable table;
	for (const auto &entry : entries) ComputeLanguageInsert(table, entry.first, One(entry.second), 14, 1, true);
	return table;
}

} // namespace

TEST(LanguageTableReplace, Rules)
{
	EXPECT_FALSE(ComputeLanguageReplaces({ One("deh"), kLanguageDehackedPass, 0 }, 1, 9, true));
	EXPECT_TRUE(ComputeLanguageReplaces({ One("x"), 5, 3, }, 5, 1, false));
	EXPECT_TRUE(ComputeLanguageReplaces({ One("x"), 14, 3, }, 2, 1, false));
	EXPECT_FALSE(ComputeLanguageReplaces({ One("fr"), 2, 1 }, 14, 2, false));
	EXPECT_TRUE(ComputeLanguageReplaces({ One("fr"), 2, 1 }, 14, 2, true));
	EXPECT_FALSE(ComputeLanguageReplaces({ One("fr"), 2, 2 }, 14, 2, true));
}

TEST(LanguageTableReplace, LaterDefaultTextBeatsOlderTranslation)
{
	// [Doruk-Aksoy] Language French: the IWAD's French text, then a mod's '[enu default]' replacement of it.
	LanguageTable table;
	ComputeLanguageInsert(table, "GOTARMOR", One("Armure"), 2, 1, false);
	ComputeLanguageInsert(table, "GOTARMOR", One("Picked up mod armor."), 14, 2, true);
	EXPECT_STREQ(ComputeLanguageFind(table, "gotarmor", 0), "Picked up mod armor.");

	ComputeLanguageInsert(table, "GOTARMOR", One("Armure du mod"), 2, 3, false);
	ComputeLanguageInsert(table, "GOTARMOR", One("ignored"), 14, 3, true);
	EXPECT_STREQ(ComputeLanguageFind(table, "GOTARMOR", 0), "Armure du mod");
}

TEST(LanguageTableEdit, RemoveOnlyTheSamePass)
{
	LanguageTable table;
	ComputeLanguageInsert(table, "A", One("a"), 3, 1, false);
	ComputeLanguageRemove(table, "A", 2);
	EXPECT_EQ(table.size(), 1u);
	ComputeLanguageRemove(table, "a", 3);
	EXPECT_TRUE(table.empty());
	ComputeLanguageRemove(table, "missing", 3);
}

TEST(LanguageTableEdit, PurgeOlder)
{
	LanguageTable table;
	ComputeLanguageInsert(table, "OLD", One("o"), 3, 1, false);
	ComputeLanguageInsert(table, "SAME", One("s"), 3, 2, false);
	ComputeLanguageSet(table, "DEH", "d");
	ComputeLanguagePurgeOlder(table, "OLD", 2);
	ComputeLanguagePurgeOlder(table, "SAME", 2);
	ComputeLanguagePurgeOlder(table, "DEH", 2);
	ComputeLanguagePurgeOlder(table, "MISSING", 2);
	EXPECT_EQ(ComputeLanguageFind(table, "OLD", 0), nullptr);
	EXPECT_STREQ(ComputeLanguageFind(table, "SAME", 0), "s");
	EXPECT_STREQ(ComputeLanguageFind(table, "DEH", 0), "d");
}

TEST(LanguageTableEdit, DehackedSurvivesReload)
{
	LanguageTable table;
	ComputeLanguageInsert(table, "A", One("lump"), 14, 1, true);
	ComputeLanguageInsert(table, "B", One("lump"), 14, 1, true);
	ComputeLanguageSet(table, "A", "dehacked");
	ComputeLanguageInsert(table, "A", One("lump again"), 1, 2, true);
	ComputeLanguageClearLoaded(table);
	EXPECT_EQ(table.size(), 1u);
	EXPECT_STREQ(ComputeLanguageFind(table, "A", 0), "dehacked");
}

TEST(LanguageTableGender, Index)
{
	EXPECT_EQ(ComputeLanguageGenderIndex(0), 0u);
	EXPECT_EQ(ComputeLanguageGenderIndex(1), 1u);
	EXPECT_EQ(ComputeLanguageGenderIndex(2), 3u);
	EXPECT_EQ(ComputeLanguageGenderIndex(-1), 0u);
	EXPECT_EQ(ComputeLanguageGenderIndex(7), 0u);
}

TEST(LanguageTableGender, FindPicksTheColumn)
{
	LanguageTable table;
	ComputeLanguageInsert(table, "KILLED", LanguageGenderStrings{ "zabil", "zabila", "zabilo", "zabilo2" }, 14, 1, true);
	ComputeLanguageInsert(table, "PLAIN", One("plain"), 14, 1, true);
	EXPECT_STREQ(ComputeLanguageFind(table, "KILLED", 1), "zabila");
	EXPECT_STREQ(ComputeLanguageFind(table, "KILLED", 3), "zabilo2");
	EXPECT_STREQ(ComputeLanguageFind(table, "PLAIN", 3), "plain");
}

TEST(LanguageTableAlias, Target)
{
	EXPECT_STREQ(ComputeLanguageAliasTarget("$$OTHER"), "OTHER");
	EXPECT_EQ(ComputeLanguageAliasTarget("$OTHER"), nullptr);
	EXPECT_EQ(ComputeLanguageAliasTarget("$"), nullptr);
	EXPECT_EQ(ComputeLanguageAliasTarget(""), nullptr);
}

TEST(LanguageTableAlias, FollowsOtherLabels)
{
	const LanguageTable table = Aliases({ { "TXT_ARMOR", "Armor" }, { "PICKUP_ARMOR", "$$txt_armor" }, { "HUD_ARMOR", "$$PICKUP_ARMOR" }, { "BROKEN", "$$NOPE" }, { "EMPTY", "$$" } });
	EXPECT_STREQ(ComputeLanguageFind(table, "HUD_ARMOR", 0), "Armor");
	EXPECT_STREQ(ComputeLanguageFind(table, "pickup_armor", 0), "Armor");
	EXPECT_EQ(ComputeLanguageFind(table, "BROKEN", 0), nullptr);
	EXPECT_EQ(ComputeLanguageFind(table, "EMPTY", 0), nullptr);
	EXPECT_EQ(ComputeLanguageFind(table, "NOPE", 0), nullptr);
	EXPECT_EQ(ComputeLanguageFind(table, nullptr, 0), nullptr);
}

TEST(LanguageTableAlias, LoopEndsAsMissing)
{
	// [Doruk-Aksoy] Upstream's lookup recursed through these forever and crashed with a stack overflow.
	const LanguageTable table = Aliases({ { "A", "$$B" }, { "B", "$$A" }, { "SELF", "$$self" } });
	EXPECT_EQ(ComputeLanguageFind(table, "A", 0), nullptr);
	EXPECT_EQ(ComputeLanguageFind(table, "SELF", 0), nullptr);
}

TEST(LanguageTableAlias, ChainUpToTheLimit)
{
	LanguageTable table;
	ComputeLanguageInsert(table, "L0", One("end"), 14, 1, true);
	for (int i = 1; i <= kLanguageMaxAliasDepth + 1; ++i)
	{
		ComputeLanguageInsert(table, "L" + std::to_string(i), One(("$$L" + std::to_string(i - 1)).c_str()), 14, 1, true);
	}
	EXPECT_STREQ(ComputeLanguageFind(table, ("L" + std::to_string(kLanguageMaxAliasDepth)).c_str(), 0), "end");
	EXPECT_EQ(ComputeLanguageFind(table, ("L" + std::to_string(kLanguageMaxAliasDepth + 1)).c_str(), 0), nullptr);
}

TEST(LanguageTableMatch, IgnoresCase)
{
	LanguageTable table;
	ComputeLanguageInsert(table, "GOTARMOR", One("Picked up the armor."), 14, 1, true);
	EXPECT_STREQ(ComputeLanguageMatch(table, "picked up the ARMOR."), "GOTARMOR");
	EXPECT_EQ(ComputeLanguageMatch(table, "Picked up the armor"), nullptr);
}
