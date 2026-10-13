// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "features/hwrender/computation/worldpipelines_compute.h"

using namespace zx::hwrender;

// [rc4l] Only the level's surfaces take the cull mode, only decals are biased, only blended pipelines skip depth writes, and every one has its own name.
TEST(WorldPipelines, EachPipelineIsBuiltForWhatItDraws)
{
	std::set<std::string> names;
	for (int p = 0; p < kWorldPipelines; p++)
	{
		const WorldPipelineDesc d = ComputeWorldPipelineDesc(p);
		names.insert(d.name);
		EXPECT_EQ(p <= kWpAdd || p == kWpMaskedGBuffer, d.level) << d.name;
		EXPECT_EQ(p >= kWpMaskedDecal && p <= kWpAddRedAlpha, d.depthBias) << d.name;
		EXPECT_EQ(p == kWpRevSubNoCull || p == kWpSubNoCull, d.blend == kWpBlendRevSub || d.blend == kWpBlendSub) << d.name;
	}
	EXPECT_EQ((size_t)kWorldPipelines, names.size());
	EXPECT_EQ(kWpStageOpaque, ComputeWorldPipelineDesc(kWpOpaque).stage);
	EXPECT_EQ(kWpBlendAdd, ComputeWorldPipelineDesc(kWpAddNoCull).blend);
	EXPECT_EQ(kWpBlendAlpha, ComputeWorldPipelineDesc(kWpTransRedAlpha).blend);
	EXPECT_EQ(kWpStageRedAlpha, ComputeWorldPipelineDesc(kWpAddRedAlpha).stage);
	EXPECT_EQ(kWpStageGBuffer, ComputeWorldPipelineDesc(kWpMaskedGBuffer).stage);
	EXPECT_EQ(kWpBlendNone, ComputeWorldPipelineDesc(kWpMaskedDecal).blend);
	// [rc4l] Out of range reads as the opaque one rather than past the table.
	EXPECT_STREQ(ComputeWorldPipelineDesc(kWpOpaque).name, ComputeWorldPipelineDesc(-1).name);
	EXPECT_STREQ(ComputeWorldPipelineDesc(kWpOpaque).name, ComputeWorldPipelineDesc(kWorldPipelines).name);
}

TEST(WorldPipelines, ABatchIsMaskedAdditiveOrTranslucent)
{
	EXPECT_EQ(kWpMasked, ComputeBatchPipeline(0));
	EXPECT_EQ(kWpAdd, ComputeBatchPipeline(2));
	EXPECT_EQ(kWpTrans, ComputeBatchPipeline(1));
	EXPECT_EQ(kWpTrans, ComputeBatchPipeline(3));
}

// [rc4l] A red-alpha mask wins over a decal's bias, which wins over a sprite's, each additive or not.
TEST(WorldPipelines, ADynamicDrawTakesItsMaskDecalOrSpritePipeline)
{
	EXPECT_EQ(kWpAddRedAlpha, ComputeDynamicPipeline(2, true, true));
	EXPECT_EQ(kWpTransRedAlpha, ComputeDynamicPipeline(1, true, false));
	EXPECT_EQ(kWpAddDecal, ComputeDynamicPipeline(2, false, true));
	EXPECT_EQ(kWpTransDecal, ComputeDynamicPipeline(0, false, true));
	EXPECT_EQ(kWpAddNoCull, ComputeDynamicPipeline(2, false, false));
	EXPECT_EQ(kWpTransNoCull, ComputeDynamicPipeline(3, false, false));
	// [rc4l] The subtract styles have sprite pipelines of their own, and a decal in one draws translucent.
	EXPECT_EQ(kWpRevSubNoCull, ComputeDynamicPipeline(4, false, false));
	EXPECT_EQ(kWpSubNoCull, ComputeDynamicPipeline(5, false, false));
	EXPECT_EQ(kWpTransDecal, ComputeDynamicPipeline(4, false, true));
	EXPECT_EQ(kWpTransRedAlpha, ComputeDynamicPipeline(5, true, false));
}
