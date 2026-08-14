// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/server-browser/computation/flaghelp_compute.h"

using namespace zx;

TEST(FlagHelp, KnowsTheFlagsSomebodyWouldOpenTheBoxFor)
{
	EXPECT_STRNE("", FlagHelp("sv_nomonsters"));
	EXPECT_STRNE("", FlagHelp("compat_plasmabump"));
	EXPECT_STRNE("", FlagHelp("lms_allowrailgun"));
	EXPECT_STRNE("", FlagHelp("sv_nochangemapvote"));
}

TEST(FlagHelp, SaysNothingRatherThanSomethingEmpty)
{
	// A flag this build has and this table does not name shows no tooltip at all. An empty box
	// hovering over a switch is worse than no box: it reads as an explanation that failed.
	EXPECT_STREQ("", FlagHelp("sv_this_is_not_a_flag"));
	EXPECT_STREQ("", FlagHelp(""));
}

TEST(FlagHelp, EveryLineIsOneShortSentence)
{
	const std::vector<std::pair<std::string, std::string> > &table = FlagHelpTable();
	ASSERT_FALSE(table.empty());

	for (size_t i = 0; i < table.size(); ++i)
	{
		const std::string &name = table[i].first;
		const std::string &text = table[i].second;

		EXPECT_FALSE(text.empty()) << name;
		EXPECT_LE(text.size(), size_t(90)) << name << " is longer than a tooltip line: " << text;
		EXPECT_EQ('.', text[text.size() - 1]) << name << " does not end in a full stop: " << text;
		EXPECT_TRUE((text[0] >= 'A') && (text[0] <= 'Z')) << name << " does not start a sentence";

		// The name IS the label on the pill, so repeating it in the tooltip spends the one line
		// there is on something already on screen. The two exceptions say which other flag shares
		// their bit, which is the point of mentioning a name at all.
		if ((name != "sv_nokill") && (name != "sv_disallowsuicide") && (name != "sv_forcegldefaults"))
			EXPECT_EQ(std::string::npos, text.find(name)) << name << " repeats itself: " << text;
	}
}

TEST(FlagFieldHelp, DescribesEveryFieldTheBoxCanShow)
{
	// The nine the walk can produce. A field with no line leaves its heading unexplained, which is
	// the one row in the box somebody reads before deciding whether to open it.
	static const char *const kFields[] = { "dmflags", "dmflags2", "zadmflags", "compatflags",
		"compatflags2", "zacompatflags", "sv_forbidvoteflags", "lmsallowedweapons",
		"lmsspectatorsettings" };

	for (size_t i = 0; i < sizeof(kFields) / sizeof(kFields[0]); ++i)
	{
		const std::string text = FlagFieldHelp(kFields[i]);

		EXPECT_FALSE(text.empty()) << kFields[i];
		EXPECT_LE(text.size(), size_t(90)) << kFields[i];
		EXPECT_EQ('.', text[text.size() - 1]) << kFields[i];
	}
}

TEST(FlagFieldHelp, SaysNothingForAFieldItDoesNotKnow)
{
	EXPECT_STREQ("", FlagFieldHelp("paletteflash"));
	EXPECT_STREQ("", FlagFieldHelp(""));
}

TEST(FlagHelp, NamesNothingTwice)
{
	const std::vector<std::pair<std::string, std::string> > &table = FlagHelpTable();

	for (size_t i = 1; i < table.size(); ++i)
		EXPECT_NE(table[i - 1].first, table[i].first) << "listed twice: " << table[i].first;
}

TEST(FlagHelp, IsSortedSoTheLookupCanBisect)
{
	const std::vector<std::pair<std::string, std::string> > &table = FlagHelpTable();

	for (size_t i = 1; i < table.size(); ++i)
		EXPECT_LT(table[i - 1].first, table[i].first) << "out of order at " << table[i].first;
}

TEST(FlagHelp, FindsEveryEntryItHolds)
{
	// The bisection against the table it bisects: a sort order the search disagreed with would
	// lose entries in the middle rather than at the ends, which is exactly the fault nobody spots.
	const std::vector<std::pair<std::string, std::string> > &table = FlagHelpTable();

	for (size_t i = 0; i < table.size(); ++i)
		EXPECT_EQ(table[i].second, std::string(FlagHelp(table[i].first))) << table[i].first;
}
