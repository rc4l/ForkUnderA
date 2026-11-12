// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-session/computation/floodcheck_compute.h"

#include <gtest/gtest.h>

using zx::net::ComputeTimeInstancesFlood;

TEST(FloodCheck, NoEarlierInstanceIsNoFlood)
{
	const int none[3] = { 0, 0, 0 };
	EXPECT_FALSE(ComputeTimeInstancesFlood(none, 1000));
}

TEST(FloodCheck, ASecondInstanceWithinSevenTicsFloods)
{
	const int last[3] = { 0, 0, 993 };
	EXPECT_TRUE(ComputeTimeInstancesFlood(last, 1000));
	const int earlier[3] = { 0, 0, 992 };
	EXPECT_FALSE(ComputeTimeInstancesFlood(earlier, 1000));
}

TEST(FloodCheck, AThirdInstanceWithinFortyTwoTicsFloods)
{
	const int two[3] = { 0, 958, 990 };
	EXPECT_TRUE(ComputeTimeInstancesFlood(two, 1000));
	const int spread[3] = { 0, 957, 990 };
	EXPECT_FALSE(ComputeTimeInstancesFlood(spread, 1000));
}

TEST(FloodCheck, AFourthInstanceWithinAHundredAndFiveTicsFloods)
{
	const int three[3] = { 895, 950, 990 };
	EXPECT_TRUE(ComputeTimeInstancesFlood(three, 1000));
	const int spread[3] = { 894, 950, 990 };
	EXPECT_FALSE(ComputeTimeInstancesFlood(spread, 1000));
}
