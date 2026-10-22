// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-protocol/computation/testfaults_compute.h"

#include <gtest/gtest.h>

using namespace zx::netprotocol;

TEST(ComputeNetDropMatches, OrdinaryAndExtendedIdsFromAList)
{
	EXPECT_TRUE(ComputeNetDropMatches("47", 47, -1));
	EXPECT_FALSE(ComputeNetDropMatches("47", 47, 3));
	EXPECT_TRUE(ComputeNetDropMatches("12,200/9", 200, 9));
	EXPECT_FALSE(ComputeNetDropMatches("12,200/9", 200, 8));
	EXPECT_TRUE(ComputeNetDropMatches("12,200/9", 12, -1));
}

TEST(ComputeNetDropMatches, EmptyOrMalformedDropsNothing)
{
	EXPECT_FALSE(ComputeNetDropMatches("", 0, -1));
	EXPECT_FALSE(ComputeNetDropMatches(nullptr, 0, -1));
	EXPECT_FALSE(ComputeNetDropMatches("abc", 0, -1));
	EXPECT_FALSE(ComputeNetDropMatches("200/x", 200, -1));
	EXPECT_FALSE(ComputeNetDropMatches("5;6", 6, -1));
	EXPECT_FALSE(ComputeNetDropMatches("5,", 6, -1));
}

TEST(ComputeReportedServerTic, TheHeldTicLessTheOffsetNeverBelowZero)
{
	EXPECT_EQ(700, ComputeReportedServerTic(700, 0));
	EXPECT_EQ(693, ComputeReportedServerTic(700, 7));
	EXPECT_EQ(700, ComputeReportedServerTic(700, -4));
	EXPECT_EQ(0, ComputeReportedServerTic(3, 10));
}
