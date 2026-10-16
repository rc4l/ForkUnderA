// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mcp-bridge/computation/ticprof_compute.h"

#include <gtest/gtest.h>

using namespace zx::mcp;

TEST(TicProf, ATicsPhasesAreWhatEachZoneGatheredSinceTheLastTic)
{
	const int64_t before[kTicZoneCount] = { 1000000, 0, 500000, 7, 0 };
	const int64_t after[kTicZoneCount] = { 4500000, 2000000, 1500000, 7, 250000 };
	const TicPhases t = ComputeTicPhases(before, after);
	EXPECT_DOUBLE_EQ(t.ms[0], 3.5);
	EXPECT_DOUBLE_EQ(t.ms[1], 2.0);
	EXPECT_DOUBLE_EQ(t.ms[2], 1.0);
	EXPECT_DOUBLE_EQ(t.ms[3], 0.0);
	EXPECT_DOUBLE_EQ(t.ms[4], 0.25);
	EXPECT_STREQ(kTicZones[0], "sim/gticker");
}

// [rc4l] fuactl ticprof and the scenarios read these keys, so the JSON is the shape the bridge always sent.
TEST(TicProf, TheReportKeepsTheBridgesKeys)
{
	TicPhases a = { { 3.5, 2.0, 1.0, 0.0, 0.25 } };
	TicPhases b = { { 1.0, 0.0, 0.0, 0.0, 0.0 } };
	EXPECT_EQ(ComputeTicProfJson({ a, b }),
		"{\"tics\":[{\"total\":3.500000,\"pticker\":2.000000,\"thinkers\":1.000000,\"effects\":0.000000,\"specials\":0.250000},"
		"{\"total\":1.000000,\"pticker\":0.000000,\"thinkers\":0.000000,\"effects\":0.000000,\"specials\":0.000000}]}");
	EXPECT_EQ(ComputeTicProfJson({}), "{\"tics\":[]}");
}
