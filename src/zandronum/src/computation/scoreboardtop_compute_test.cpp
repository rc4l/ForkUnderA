// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/scoreboardtop_compute.h"

#include <gtest/gtest.h>

using zx::ComputeScoreboardTop;

TEST(ScoreboardTop, AnOrdinaryTitleKeepsTheBoardBelowIt)
{
	EXPECT_EQ(ComputeScoreboardTop(90, 3, 20), 90);
	EXPECT_EQ(ComputeScoreboardTop(90, 3, -1), 90);
}

TEST(ScoreboardTop, AWholeScreenTitleStartsTheBoardNearTheTop)
{
	EXPECT_EQ(ComputeScoreboardTop(600, 3, 200), 96);
	EXPECT_EQ(ComputeScoreboardTop(600, 3, 240), 96);
}

TEST(ScoreboardTop, OnlyWhenTheBoardWouldStartAScreenDown)
{
	EXPECT_EQ(ComputeScoreboardTop(599, 3, 200), 599);
	EXPECT_EQ(ComputeScoreboardTop(600, 3, 199), 600);
	EXPECT_EQ(ComputeScoreboardTop(-1, 0, 200), -1);
}
