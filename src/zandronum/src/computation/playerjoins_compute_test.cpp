// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/playerjoins_compute.h"

#include <gtest/gtest.h>

using namespace zx::playerjoins;

TEST(PlayerJoinsEventTeam, AJoinerOnATeamNamesIt)
{
	EXPECT_EQ(ComputePlayerJoinsEventTeam(true, 1, 255), 1u);
}

TEST(PlayerJoinsEventTeam, ARandomTeamOrNoTeamsNamesNone)
{
	// [rc4l] teams.Size(), the random team, is no valid team.
	EXPECT_EQ(ComputePlayerJoinsEventTeam(false, 2, 255), 255u);
	EXPECT_EQ(ComputePlayerJoinsEventTeam(false, 0, 255), 255u);
}
