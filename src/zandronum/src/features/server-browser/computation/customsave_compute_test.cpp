// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/server-browser/computation/customsave_compute.h"
#include "features/addon-catalogue/computation/addonfile_compute.h"

using namespace zx;

namespace
{

CustomEntry Sample()
{
	CustomEntry entry;
	entry.name = "Sunday co-op";
	entry.iwad = "doom2.wad";
	entry.files.push_back(CustomFile("av.wad", "2c0a712d3e39b010519b879e0ff7dd51"));
	entry.files.push_back(CustomFile("av20.wad", "5b9b0a2e29e8ee06b0b9b4b4d0a0a1b2"));
	entry.maps.push_back("MAP01");
	entry.maps.push_back("MAP02");
	entry.gameMode = "survival";
	entry.bPvP = false;
	entry.cvars.push_back(std::make_pair("dmflags2", "64"));
	entry.cvars.push_back(std::make_pair("sv_maxlives", "3"));

	return entry;
}

std::vector<std::string> Taken()
{
	std::vector<std::string> out;
	out.push_back("Sunday co-op");
	out.push_back("Duel night");

	return out;
}

} // namespace

// ---------------------------------------------------------------- the entry file

TEST(CustomAddonJson, ReadsBackThroughTheCatalogueSOwnParser)
{
	// [rc4l] The whole point of the shape: a saved preset is a catalogue entry, so the code that
	// reads the catalogue reads this. If this test fails, a preset is a file only this can open.
	const AddonEntry parsed = ParseAddonFile("sunday", CustomAddonJson(Sample()));

	EXPECT_EQ("Sunday co-op", parsed.name);
	EXPECT_EQ("doom2.wad", parsed.iwad);
	EXPECT_EQ("MAP01", parsed.map) << "the first map in the rotation is where it opens";

	ASSERT_EQ(size_t(2), parsed.files.size());
	EXPECT_EQ("av.wad", parsed.files[0].name);
	EXPECT_EQ("2c0a712d3e39b010519b879e0ff7dd51", parsed.files[0].md5)
		<< "without the hash a missing file cannot be fetched";
	EXPECT_EQ("av20.wad", parsed.files[1].name);
}

TEST(CustomAddonJson, EscapesWhatWouldOtherwiseBreakTheFile)
{
	CustomEntry entry = Sample();
	entry.name = "He said \"hello\"";

	const AddonEntry parsed = ParseAddonFile("x", CustomAddonJson(entry));

	EXPECT_EQ("He said \"hello\"", parsed.name);
}

TEST(CustomAddonJson, SaysWhichKindItIs)
{
	// The catalogue requires it of every entry and only knows two, so a preset that said anything
	// else would be refused by the reader rather than shown.
	CustomEntry entry = Sample();
	entry.bPvP = true;

	EXPECT_EQ(VariantKind::PvP, ParseAddonFile("x", CustomAddonJson(entry)).kind);

	entry.bPvP = false;
	EXPECT_EQ(VariantKind::PvE, ParseAddonFile("x", CustomAddonJson(entry)).kind);
}

TEST(NextSaveState, RefusesAConfigurationWithNoFilesBeforeItAsksForAName)
{
	// A preset IS its files: the catalogue schema requires one with a hash, which is what lets a
	// missing file be fetched later. Refused at the box rather than written as a folder the reader
	// then skips in silence.
	EXPECT_EQ(SaveState::NoFiles, NextSaveState("Perfectly good name", Taken(), false, 0));
	EXPECT_EQ(SaveState::NoFiles, NextSaveState("", Taken(), false, 0));
}

// ---------------------------------------------------------------- the cfg

TEST(CustomServerCfg, WritesTheSettingsAndTheRotation)
{
	const std::string cfg = CustomServerCfg(Sample());

	std::vector<std::pair<std::string, std::string> > cvars;
	std::vector<std::string> maps;
	ParseCustomCfg(cfg, cvars, maps);

	ASSERT_EQ(size_t(2), maps.size());
	EXPECT_EQ("MAP01", maps[0]);
	EXPECT_EQ("MAP02", maps[1]);

	bool flags = false;
	bool lives = false;
	bool mode = false;

	for (size_t i = 0; i < cvars.size(); ++i)
	{
		if ((cvars[i].first == "dmflags2") && (cvars[i].second == "64"))		flags = true;
		if ((cvars[i].first == "sv_maxlives") && (cvars[i].second == "3"))		lives = true;
		if ((cvars[i].first == "survival") && (cvars[i].second == "1"))			mode = true;
	}

	EXPECT_TRUE(flags);
	EXPECT_TRUE(lives);
	EXPECT_TRUE(mode) << "the gamemode is a line of the cfg like every other setting";
}

TEST(ParseCustomCfg, SkipsCommentsAndBlankLines)
{
	std::vector<std::pair<std::string, std::string> > cvars;
	std::vector<std::string> maps;

	ParseCustomCfg("// a note\n\n  // indented note\nskill 4\n", cvars, maps);

	ASSERT_EQ(size_t(1), cvars.size());
	EXPECT_EQ("skill", cvars[0].first);
	EXPECT_EQ("4", cvars[0].second);
}

TEST(CustomServerCfg, RefusesAValueThatWouldWriteALineOfItsOwn)
{
	CustomEntry entry;
	entry.cvars.push_back(std::make_pair("skill", "4\nsv_cheats 1"));
	entry.cvars.push_back(std::make_pair("has space", "1"));
	entry.cvars.push_back(std::make_pair("timelimit", "20"));

	std::vector<std::pair<std::string, std::string> > cvars;
	std::vector<std::string> maps;
	ParseCustomCfg(CustomServerCfg(entry), cvars, maps);

	ASSERT_EQ(size_t(1), cvars.size());
	EXPECT_EQ("timelimit", cvars[0].first);
}

// ---------------------------------------------------------------- the name

TEST(IsCustomName, RefusesWhatCannotBeAFolder)
{
	EXPECT_TRUE(IsCustomName("Sunday co-op"));
	EXPECT_TRUE(IsCustomName("duel_40"));

	EXPECT_FALSE(IsCustomName(""));
	EXPECT_FALSE(IsCustomName("../../boot"));
	EXPECT_FALSE(IsCustomName("bad/name"));
	EXPECT_FALSE(IsCustomName("bad\\name"));
	EXPECT_FALSE(IsCustomName("C:name"));
	EXPECT_FALSE(IsCustomName("."));
	EXPECT_FALSE(IsCustomName(".."));
	EXPECT_FALSE(IsCustomName(" leading"));
	EXPECT_FALSE(IsCustomName("trailing "));
}

// ---------------------------------------------------------------- the save question

TEST(NextSaveState, AFreeNameIsReadyToSave)
{
	EXPECT_EQ(SaveState::Ready, NextSaveState("Something new", Taken(), false, 1));
}

TEST(NextSaveState, AnEmptyBoxHasNothingToSave)
{
	EXPECT_EQ(SaveState::Empty, NextSaveState("", Taken(), false, 1));
}

TEST(NextSaveState, ANameThatCannotBeAFolderSaysSo)
{
	EXPECT_EQ(SaveState::Bad, NextSaveState("../boot", Taken(), false, 1));
}

TEST(NextSaveState, ATakenNameAsksBeforeItReplaces)
{
	// The first Confirm is a question and the second is the answer. Losing a configuration to a
	// mistyped name is the one thing here that cannot be undone.
	EXPECT_EQ(SaveState::Asking, NextSaveState("Duel night", Taken(), false, 1));
	EXPECT_EQ(SaveState::Replace, NextSaveState("Duel night", Taken(), true, 1));
}

TEST(NextSaveState, ForgettingTheQuestionIsWhatTypingDoes)
{
	// Having been asked about one name says nothing about another, so a caller that clears the
	// flag on a keystroke gets a fresh answer -- which is the behaviour the box needs.
	EXPECT_EQ(SaveState::Ready, NextSaveState("A different name", Taken(), true, 1));
}

TEST(SaveStatus, WarnsOnlyWhereThereIsSomethingToWarnAbout)
{
	EXPECT_FALSE(SaveStatusIsWarning(SaveState::Fresh));
	EXPECT_FALSE(SaveStatusIsWarning(SaveState::Ready));
	EXPECT_FALSE(SaveStatusIsWarning(SaveState::Empty));

	EXPECT_TRUE(SaveStatusIsWarning(SaveState::Bad));
	EXPECT_TRUE(SaveStatusIsWarning(SaveState::Asking));
	EXPECT_TRUE(SaveStatusIsWarning(SaveState::Replace));
}

TEST(SaveStatus, SaysNothingWhereThereIsNothingToSay)
{
	EXPECT_STREQ("", SaveStatusText(SaveState::Fresh));
	EXPECT_STREQ("", SaveStatusText(SaveState::Ready));

	EXPECT_STRNE("", SaveStatusText(SaveState::Asking));
	EXPECT_STRNE("", SaveStatusText(SaveState::Bad));
}

// ---------------------------------------------------------------- what a written cfg survives

// [rc4l] The characters a preset name can carry that would otherwise end the JSON string early.
// A file this writes must be one it can read back, whatever somebody typed into the box.
TEST(CustomAddonJson, EscapesEveryCharacterThatWouldEndTheStringEarly)
{
	CustomEntry entry;
	entry.name = "a\"b\\c\nd\re\tf";
	entry.iwad = "doom2.wad";
	entry.files.push_back(CustomFile("x.wad", "0123456789abcdef0123456789abcdef"));

	const std::string json = CustomAddonJson(entry);

	EXPECT_NE(std::string::npos, json.find("\\\""));
	EXPECT_NE(std::string::npos, json.find("\\\\"));
	EXPECT_NE(std::string::npos, json.find("\\n"));
	EXPECT_NE(std::string::npos, json.find("\\r"));
	EXPECT_NE(std::string::npos, json.find("\\t"));
}

TEST(CustomAddonJson, DropsAControlCharacterRatherThanEscapingIt)
{
	// A \u escape would be a parser this does not need, and a bell has no business in a name.
	CustomEntry entry;
	entry.name = std::string("bell") + '\a' + "end";
	entry.iwad = "doom2.wad";
	entry.files.push_back(CustomFile("x.wad", "0123456789abcdef0123456789abcdef"));

	const std::string json = CustomAddonJson(entry);

	EXPECT_NE(std::string::npos, json.find("bellend"));
	EXPECT_EQ(std::string::npos, json.find('\a'));
}

TEST(ParseCustomCfg, SkipsAnIndentedCommentAndALineWithNoValue)
{
	// The indented comment reaches the leading-whitespace skip; the bare word reaches "no space,
	// so no value". Neither may become a setting, and the value keeps none of its padding.
	std::vector<std::pair<std::string, std::string> > cvars;
	std::vector<std::string> maps;

	ParseCustomCfg("\t  // indented comment\nlonelyword\n  skill  4  \n", cvars, maps);

	ASSERT_EQ(1u, cvars.size());
	EXPECT_EQ("skill", cvars[0].first);
	EXPECT_EQ("4", cvars[0].second) << "trailing whitespace is trimmed off the value";
}

TEST(IsCustomName, RefusesAControlCharacterInside)
{
	EXPECT_FALSE(IsCustomName(std::string("bad") + '\x01' + "name"));
}

TEST(SaveStatusText, SaysSomethingForEveryStateThatNeedsIt)
{
	// Swept, so a state cannot be added without a line to show for it.
	EXPECT_STRNE("", SaveStatusText(SaveState::Empty));
	EXPECT_STRNE("", SaveStatusText(SaveState::NoFiles));
	EXPECT_STRNE("", SaveStatusText(SaveState::Replace));

	EXPECT_STREQ("", SaveStatusText(SaveState::Fresh));
	EXPECT_STREQ("", SaveStatusText(static_cast<SaveState>(999)))
		<< "a state this build has no word for says nothing rather than crashing";
}

TEST(CustomFile, DefaultsToEmpty)
{
	// The default constructor is what lets a CustomFile sit in a vector before it is filled in.
	const CustomFile file;

	EXPECT_TRUE(file.name.empty());
	EXPECT_TRUE(file.md5.empty());
}
