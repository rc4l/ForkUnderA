// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [KI] Every branch of the nettraffic percentage: equal values, the empty second, a zero whole, and the plain ratio.

#include "netstats_compute.h"
#include <gtest/gtest.h>

namespace {

using zx::netclient::ComputeSharePercent;

TEST(NetStatsCompute, EqualPartAndWholeIsAlwaysFullShare)
{
	EXPECT_FLOAT_EQ(100.0f, ComputeSharePercent(500, 500));
	EXPECT_FLOAT_EQ(100.0f, ComputeSharePercent(0, 0));
}

TEST(NetStatsCompute, ZeroWholeIsFullShareRatherThanDividing)
{
	// [KI] The trackers never report a part without its whole, so this input only guards the arithmetic against 3.3's division by zero.
	EXPECT_FLOAT_EQ(100.0f, ComputeSharePercent(7, 0));
}

TEST(NetStatsCompute, PlainRatioOtherwise)
{
	EXPECT_FLOAT_EQ(50.0f, ComputeSharePercent(250, 500));
	EXPECT_FLOAT_EQ(120.0f, ComputeSharePercent(600, 500));
	EXPECT_FLOAT_EQ(0.0f, ComputeSharePercent(0, 500));
}

} // namespace
