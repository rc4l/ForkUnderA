// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/hwrender/computation/pipelineswitch_compute.h"

#include <vector>

using namespace zx::hwrender;

// [Doruk-Aksoy] Translucent and additive sprites interleaved switch at every change, the most frequent pair first, ties in pair order.
TEST(PipelineSwitch, SwitchesAreTalliedByPairMostFrequentFirst)
{
	const std::vector<PipelineKey> draws = { { 5, -1 }, { 6, -1 }, { 5, -1 }, { 6, -1 }, { 6, -1 }, { 8, -1 }, { 5, -1 } };
	const std::vector<PipelineSwitchCount> tally = ComputePipelineSwitches(draws);
	ASSERT_EQ(4u, tally.size());
	EXPECT_EQ(5, tally[0].from); EXPECT_EQ(6, tally[0].to); EXPECT_EQ(2, tally[0].count);
	EXPECT_EQ(6, tally[1].from); EXPECT_EQ(5, tally[1].to); EXPECT_EQ(1, tally[1].count);
	EXPECT_EQ(6, tally[2].from); EXPECT_EQ(8, tally[2].to); EXPECT_EQ(1, tally[2].count);
	EXPECT_EQ(8, tally[3].from); EXPECT_EQ(5, tally[3].to); EXPECT_EQ(1, tally[3].count);
	EXPECT_EQ(5, ComputeSwitchTotal(tally));
}

// [Doruk-Aksoy] A GLDEFS shader changing within one pipeline is a switch of its own program, counted to itself.
TEST(PipelineSwitch, AShaderChangeWithinAPipelineCountsAsASwitchToItself)
{
	const std::vector<PipelineSwitchCount> tally = ComputePipelineSwitches({ { 5, -1 }, { 5, 2 }, { 5, 2 } });
	ASSERT_EQ(1u, tally.size());
	EXPECT_EQ(5, tally[0].from); EXPECT_EQ(5, tally[0].to); EXPECT_EQ(1, tally[0].count);
}

// [Doruk-Aksoy] No draws, one draw, or draws all on one program switch nothing after the first bind.
TEST(PipelineSwitch, NothingToSwitch)
{
	EXPECT_TRUE(ComputePipelineSwitches({}).empty());
	EXPECT_TRUE(ComputePipelineSwitches({ { 5, -1 } }).empty());
	EXPECT_TRUE(ComputePipelineSwitches({ { 5, -1 }, { 5, -1 } }).empty());
	EXPECT_EQ(0, ComputeSwitchTotal({}));
}
