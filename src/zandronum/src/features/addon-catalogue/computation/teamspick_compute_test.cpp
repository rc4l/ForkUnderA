// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/addon-catalogue/computation/teamspick_compute.h"

using zx::HostGameMode;
using zx::TeamsControl;
using zx::TeamsCountAtStop;
using zx::TeamsCvars;
using zx::TeamsFor;
using zx::TeamsShape;
using zx::TeamsStopCount;
using zx::TeamsStopForCount;

namespace
{

typedef std::vector<std::pair<std::string, std::string> > Cvars;

std::string ValueOf(const Cvars &cvars, const std::string &name)
{
	for (size_t i = 0; i < cvars.size(); ++i)
	{
		if (cvars[i].first == name)
			return cvars[i].second;
	}
	return std::string();
}

} // namespace

// ------------------------------------------------------------ the stops

TEST(TeamsPick, TheStopsSkipOneAltogether)
{
	// The whole reason this axis is an index rather than a count.
	ASSERT_EQ(4, TeamsStopCount());
	EXPECT_EQ(0, TeamsCountAtStop(0));
	EXPECT_EQ(2, TeamsCountAtStop(1));
	EXPECT_EQ(3, TeamsCountAtStop(2));
	EXPECT_EQ(4, TeamsCountAtStop(3));
}

TEST(TeamsPick, AStopOffEitherEndGivesTheNearestEnd)
{
	EXPECT_EQ(0, TeamsCountAtStop(-7));
	EXPECT_EQ(4, TeamsCountAtStop(99));
}

TEST(TeamsPick, ACountFindsItsOwnStop)
{
	EXPECT_EQ(0, TeamsStopForCount(0));
	EXPECT_EQ(1, TeamsStopForCount(2));
	EXPECT_EQ(2, TeamsStopForCount(3));
	EXPECT_EQ(3, TeamsStopForCount(4));
}

TEST(TeamsPick, OneTeamIsAFreeForAllRatherThanASide)
{
	EXPECT_EQ(0, TeamsStopForCount(1));
}

TEST(TeamsPick, MoreTeamsThanExistTakesTheCeiling)
{
	// sv_maxteams clamps itself to teams.Size(), so asking for eight would be ignored rather than
	// refused. Pinned at the ceiling here instead, where it can be seen.
	EXPECT_EQ(3, TeamsStopForCount(8));
}

// ------------------------------------------------------------ whether there is a control

TEST(TeamsPick, AWayOfPlayingThatDidNotAskGetsNoControl)
{
	const TeamsControl c = TeamsFor(HostGameMode::Deathmatch, false, -1);

	EXPECT_EQ(TeamsShape::None, c.shape);
	EXPECT_FALSE(c.applies);
	EXPECT_FALSE(c.adjustable);
	EXPECT_FALSE(c.reason.empty());
}

TEST(TeamsPick, AModeWithNoFreeForAllToLeaveGetsNoControlEvenWhenAsked)
{
	// Capture the Flag honours sv_maxteams perfectly well; its MAPS do not, and a third team would
	// spawn with no flag to take.
	const TeamsControl c = TeamsFor(HostGameMode::CaptureTheFlag, true, -1);

	EXPECT_EQ(TeamsShape::None, c.shape);
	EXPECT_FALSE(c.applies);
	EXPECT_FALSE(c.reason.empty());
}

TEST(TeamsPick, TheModesWhoseSidesComeFromTheMapSaySoInThoseWords)
{
	// Both of these have sides, so the generic refusal would send an author looking for a mistake
	// that is not there, which is the whole point of the branch.
	const HostGameMode fromTheMap[] = { HostGameMode::CaptureTheFlag, HostGameMode::Skulltag };

	for (size_t i = 0; i < sizeof(fromTheMap) / sizeof(fromTheMap[0]); ++i)
	{
		const TeamsControl c = TeamsFor(fromTheMap[i], true, 4);

		EXPECT_FALSE(c.applies) << "mode index " << i;
		EXPECT_NE(std::string::npos, c.reason.find("map decides")) << "mode index " << i;
		EXPECT_TRUE(TeamsCvars(c).empty()) << "mode index " << i;
	}
}

TEST(TeamsPick, PossessionSwitchesToItsOwnTeamMode)
{
	// The third mode with a twin, and it qualifies for the same reason the other two do: the stone
	// is spawned by the engine at a deathmatch start, so no part of the map has to agree.
	const Cvars solo = TeamsCvars(TeamsFor(HostGameMode::Possession, true, 0));
	const Cvars teams = TeamsCvars(TeamsFor(HostGameMode::Possession, true, 3));

	EXPECT_EQ("true", ValueOf(solo, "possession"));
	EXPECT_EQ("true", ValueOf(teams, "teampossession"));
	EXPECT_EQ("3", ValueOf(teams, "sv_maxteams"));
}

TEST(TeamsPick, AModeThatIsAlreadyTeamsHasNothingToSwitch)
{
	// Handed the team half of a pair, there is no free-for-all to go back to: the control would be
	// offering to turn the chosen gamemode into a different one.
	EXPECT_FALSE(TeamsFor(HostGameMode::TeamPossession, true, 2).applies);
	EXPECT_FALSE(TeamsFor(HostGameMode::TeamLastManStanding, true, 2).applies);
	EXPECT_FALSE(TeamsFor(HostGameMode::TeamDeathmatch, true, 2).applies);
}

TEST(TeamsPick, TerminatorHasNoTeamsBecauseThatIsTheMode)
{
	// One ball, and whoever holds it is everyone else's enemy. Sides would remove the mode.
	const TeamsControl c = TeamsFor(HostGameMode::Terminator, true, 4);

	EXPECT_FALSE(c.applies);
	EXPECT_FALSE(c.reason.empty());
}

TEST(TeamsPick, AnUnstatedGamemodeSaysSoRatherThanGuessing)
{
	const TeamsControl c = TeamsFor(HostGameMode::Unknown, true, -1);

	EXPECT_FALSE(c.applies);
	EXPECT_NE(std::string::npos, c.reason.find("does not say"));
}

// ------------------------------------------------------------ what it starts on

TEST(TeamsPick, NothingChosenIsAFreeForAll)
{
	const TeamsControl c = TeamsFor(HostGameMode::Deathmatch, true, -1);

	EXPECT_EQ(TeamsShape::Optional, c.shape);
	EXPECT_TRUE(c.applies);
	EXPECT_TRUE(c.adjustable);
	EXPECT_EQ(0, c.count);
	EXPECT_EQ(0, c.stop);
	EXPECT_TRUE(c.reason.empty());
}

TEST(TeamsPick, AChoiceIsKept)
{
	const TeamsControl c = TeamsFor(HostGameMode::Deathmatch, true, 3);

	EXPECT_EQ(3, c.count);
	EXPECT_EQ(2, c.stop);
}

TEST(TeamsPick, AChoiceBiggerThanTheCeilingLandsOnIt)
{
	const TeamsControl c = TeamsFor(HostGameMode::LastManStanding, true, 40);

	EXPECT_EQ(4, c.count);
	EXPECT_EQ(3, c.stop);
}

// ------------------------------------------------------------ the cvars

TEST(TeamsPick, DeathmatchSwitchesBetweenDeathmatchAndTeamplay)
{
	const Cvars solo = TeamsCvars(TeamsFor(HostGameMode::Deathmatch, true, 0));
	const Cvars teams = TeamsCvars(TeamsFor(HostGameMode::Deathmatch, true, 2));

	EXPECT_EQ("true", ValueOf(solo, "deathmatch"));
	EXPECT_EQ("", ValueOf(solo, "teamplay"));
	EXPECT_EQ("", ValueOf(solo, "sv_maxteams"));

	EXPECT_EQ("true", ValueOf(teams, "teamplay"));
	EXPECT_EQ("2", ValueOf(teams, "sv_maxteams"));
}

TEST(TeamsPick, LastManStandingSwitchesToItsOwnTeamMode)
{
	const Cvars solo = TeamsCvars(TeamsFor(HostGameMode::LastManStanding, true, 0));
	const Cvars teams = TeamsCvars(TeamsFor(HostGameMode::LastManStanding, true, 4));

	EXPECT_EQ("true", ValueOf(solo, "lastmanstanding"));
	EXPECT_EQ("true", ValueOf(teams, "teamlms"));
	EXPECT_EQ("4", ValueOf(teams, "sv_maxteams"));
}

TEST(TeamsPick, TheGamemodeIsNamedBeforeTheCount)
{
	// sv_maxteams is latched and clamps against TEAMINFO; setting it after the mode keeps the order
	// the host's arguments have always used.
	const Cvars teams = TeamsCvars(TeamsFor(HostGameMode::Deathmatch, true, 3));

	ASSERT_EQ(2u, teams.size());
	EXPECT_EQ("teamplay", teams[0].first);
	EXPECT_EQ("sv_maxteams", teams[1].first);
}

TEST(TeamsPick, AControlThatDoesNotApplyWritesNothing)
{
	// The panel must never claim something the server will not do.
	EXPECT_TRUE(TeamsCvars(TeamsFor(HostGameMode::Duel, true, 4)).empty());
	EXPECT_TRUE(TeamsCvars(TeamsFor(HostGameMode::Deathmatch, false, 4)).empty());
}
