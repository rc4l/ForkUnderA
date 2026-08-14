// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/server-browser/computation/servervar_compute.h"

using namespace zx;

// ---------------------------------------------------------------- the table

TEST(ServerVarTable, HasTheSettingsOurOwnExperiencesActuallySet)
{
	const std::vector<ServerVar> &table = ServerVarTable();
	ASSERT_FALSE(table.empty());

	bool respawnTime = false;
	bool teamDamage = false;
	bool mapVote = false;

	for (size_t i = 0; i < table.size(); ++i)
	{
		if (table[i].name == "sv_forcerespawntime")	respawnTime = true;
		if (table[i].name == "teamdamage")			teamDamage = true;
		if (table[i].name == "sv_nomapvote")		mapVote = true;
	}

	EXPECT_TRUE(respawnTime);
	EXPECT_TRUE(teamDamage);
	EXPECT_TRUE(mapVote);
}

TEST(ServerVarTable, DoesNotOfferTheCvarThatDoesNotExist)
{
	// sv_teamdamage is not a cvar. One of our own cfgs set it for years and it did nothing; the
	// real setting is teamdamage. Offering the wrong name here would make that mistake permanent.
	const std::vector<ServerVar> &table = ServerVarTable();

	for (size_t i = 0; i < table.size(); ++i)
		EXPECT_NE("sv_teamdamage", table[i].name);
}

TEST(ServerVarTable, TeamDamageIsAScalarRatherThanASwitch)
{
	const std::vector<ServerVar> &table = ServerVarTable();

	for (size_t i = 0; i < table.size(); ++i)
	{
		if (table[i].name != "teamdamage")
			continue;

		EXPECT_EQ(VarKind::Fraction, table[i].kind) << "0 is none and 1 is full, not on and off";
		return;
	}

	FAIL() << "teamdamage is missing";
}

TEST(ServerVarTable, EveryRowSaysWhatItIsAndWhatItStartsAt)
{
	const std::vector<ServerVar> &table = ServerVarTable();

	for (size_t i = 0; i < table.size(); ++i)
	{
		EXPECT_FALSE(table[i].name.empty());
		EXPECT_FALSE(table[i].label.empty()) << table[i].name;
		EXPECT_TRUE(ServerVarAccepts(table[i].kind, table[i].fallback))
			<< table[i].name << " starts at something it would refuse";
	}
}

TEST(ServerVarTable, NamesNothingTwice)
{
	const std::vector<ServerVar> &table = ServerVarTable();

	for (size_t i = 0; i < table.size(); ++i)
	{
		for (size_t j = i + 1; j < table.size(); ++j)
			EXPECT_NE(table[i].name, table[j].name) << "listed twice: " << table[i].name;
	}
}

// ---------------------------------------------------------------- what a mode shows

TEST(LimitsForMode, DeathmatchShowsFragsAndNothingElse)
{
	const ModeLimits m = LimitsForMode(true, false, false, false, false);

	EXPECT_TRUE(m.fraglimit);
	EXPECT_FALSE(m.pointlimit);
	EXPECT_FALSE(m.winlimit);
	EXPECT_FALSE(m.lives);
}

TEST(LimitsForMode, DuelShowsBothBecauseItEarnsBoth)
{
	// The case that makes this four questions rather than one choice: Duel declares FRAGS and WINS.
	const ModeLimits m = LimitsForMode(true, false, true, false, false);

	EXPECT_TRUE(m.fraglimit);
	EXPECT_TRUE(m.winlimit);
	EXPECT_FALSE(m.pointlimit);
}

TEST(LimitsForMode, DominationShowsPointsRatherThanWins)
{
	const ModeLimits m = LimitsForMode(false, true, false, false, true);

	EXPECT_TRUE(m.pointlimit);
	EXPECT_FALSE(m.winlimit);
	EXPECT_FALSE(m.fraglimit);
	EXPECT_TRUE(m.teams);
}

TEST(LimitsForMode, SurvivalShowsLives)
{
	const ModeLimits m = LimitsForMode(false, false, false, true, false);

	EXPECT_TRUE(m.lives);
	EXPECT_FALSE(m.fraglimit);
	EXPECT_FALSE(m.pointlimit);
	EXPECT_FALSE(m.winlimit);
}

TEST(LimitsForMode, LastManStandingShowsWinsAndLives)
{
	const ModeLimits m = LimitsForMode(false, false, true, true, false);

	EXPECT_TRUE(m.winlimit);
	EXPECT_TRUE(m.lives);
	EXPECT_FALSE(m.fraglimit);
}

TEST(LimitsForMode, CooperativeShowsNoneOfThem)
{
	const ModeLimits m = LimitsForMode(false, false, false, false, false);

	EXPECT_FALSE(m.fraglimit);
	EXPECT_FALSE(m.pointlimit);
	EXPECT_FALSE(m.winlimit);
	EXPECT_FALSE(m.lives);
	EXPECT_FALSE(m.teams);
}

// ---------------------------------------------------------------- what a box accepts

TEST(ServerVarAccepts, ATogglesIsZeroOrOne)
{
	EXPECT_TRUE(ServerVarAccepts(VarKind::Toggle, "0"));
	EXPECT_TRUE(ServerVarAccepts(VarKind::Toggle, "1"));
	EXPECT_FALSE(ServerVarAccepts(VarKind::Toggle, "7")) << "a switch set to seven is not a switch";
	EXPECT_FALSE(ServerVarAccepts(VarKind::Toggle, "true"));
}

TEST(ServerVarAccepts, ANumberIsDigits)
{
	EXPECT_TRUE(ServerVarAccepts(VarKind::Number, "60"));
	EXPECT_FALSE(ServerVarAccepts(VarKind::Number, "60.5")) << "a whole number has no point in it";
	EXPECT_FALSE(ServerVarAccepts(VarKind::Number, "-1"));
	EXPECT_FALSE(ServerVarAccepts(VarKind::Number, "ten"));
}

TEST(ServerVarAccepts, AFractionTakesOnePoint)
{
	EXPECT_TRUE(ServerVarAccepts(VarKind::Fraction, "0.5"));
	EXPECT_TRUE(ServerVarAccepts(VarKind::Fraction, "1"));
	EXPECT_FALSE(ServerVarAccepts(VarKind::Fraction, "0.5.1"));
}

TEST(ServerVarAccepts, AnEmptyBoxIsSomebodyStillTyping)
{
	EXPECT_TRUE(ServerVarAccepts(VarKind::Toggle, ""));
	EXPECT_TRUE(ServerVarAccepts(VarKind::Number, ""));
	EXPECT_TRUE(ServerVarAccepts(VarKind::Fraction, ""));
}
