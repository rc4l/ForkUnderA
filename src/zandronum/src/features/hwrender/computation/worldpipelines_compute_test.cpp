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
		EXPECT_EQ((p >= kWpMaskedDecal && p <= kWpAddRedAlpha) || p == kWpFuzzDecal, d.depthBias) << d.name;
		EXPECT_EQ(p == kWpFuzzNoCull || p == kWpFuzzDecal || p == kWpFuzzHud, d.blend == kWpBlendFuzz) << d.name;
		EXPECT_EQ(p >= kWpTransHud && p <= kWpSubHud, d.depthWrite) << d.name;
		EXPECT_EQ(p == kWpRevSubNoCull || p == kWpSubNoCull || p == kWpRevSubHud || p == kWpSubHud,
			d.blend == kWpBlendRevSub || d.blend == kWpBlendSub) << d.name;
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

// [Doruk-Aksoy] Every pipeline but the red-alpha decals' runs a hardware shader's own program, and nothing outside the table does.
TEST(WorldPipelines, AllButRedAlphaTakeAUserShader)
{
	for (int p = 0; p < kWorldPipelines; p++)
		EXPECT_EQ(p != kWpTransRedAlpha && p != kWpAddRedAlpha, ComputeWorldPipelineTakesUserShader(p)) << p;
	EXPECT_FALSE(ComputeWorldPipelineTakesUserShader(-1));
	EXPECT_FALSE(ComputeWorldPipelineTakesUserShader(kWorldPipelines));
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
	EXPECT_EQ(kWpAddRedAlpha, ComputeDynamicPipeline(2, true, true, false));
	EXPECT_EQ(kWpTransRedAlpha, ComputeDynamicPipeline(1, true, false, false));
	EXPECT_EQ(kWpAddDecal, ComputeDynamicPipeline(2, false, true, false));
	EXPECT_EQ(kWpTransDecal, ComputeDynamicPipeline(0, false, true, false));
	EXPECT_EQ(kWpAddNoCull, ComputeDynamicPipeline(2, false, false, false));
	EXPECT_EQ(kWpTransNoCull, ComputeDynamicPipeline(1, false, false, false));
	EXPECT_EQ(kWpTransNoCull, ComputeDynamicPipeline(0, false, false, false));
	// [rc4l] A shadow and a fuzzy decal multiply the screen, the decal biased onto its wall.
	EXPECT_EQ(kWpFuzzNoCull, ComputeDynamicPipeline(3, false, false, false));
	EXPECT_EQ(kWpFuzzDecal, ComputeDynamicPipeline(3, false, true, false));
	// [rc4l] The subtract styles have sprite pipelines of their own, and a decal in one draws translucent.
	EXPECT_EQ(kWpRevSubNoCull, ComputeDynamicPipeline(4, false, false, false));
	EXPECT_EQ(kWpSubNoCull, ComputeDynamicPipeline(5, false, false, false));
	EXPECT_EQ(kWpTransDecal, ComputeDynamicPipeline(4, false, true, false));
	EXPECT_EQ(kWpTransRedAlpha, ComputeDynamicPipeline(5, true, false, false));
	// [rc4l] An unknown mode draws translucent rather than reading past the table.
	EXPECT_EQ(kWpTransNoCull, ComputeDynamicPipeline(9, false, false, false));
}

// [rc4l] The world pipelines and the projected decals draw a style with one equation, the one GL's gl_GetRenderStyle sets.
TEST(WorldPipelines, EachBlendIsGLsEquation)
{
	const struct { int blend, src, dst, op; } want[] = {
		{ kWpBlendAlpha, kBlendFactorSrcAlpha, kBlendFactorInvSrcAlpha, kBlendOpAdd },
		{ kWpBlendAdd, kBlendFactorSrcAlpha, kBlendFactorOne, kBlendOpAdd },
		{ kWpBlendRevSub, kBlendFactorSrcAlpha, kBlendFactorOne, kBlendOpRevSubtract },
		{ kWpBlendSub, kBlendFactorSrcAlpha, kBlendFactorOne, kBlendOpSubtract },
		{ kWpBlendFuzz, kBlendFactorDestColor, kBlendFactorInvSrcAlpha, kBlendOpAdd },
	};
	for (const auto &w : want)
	{
		const BlendEquation e = ComputeBlendEquation(w.blend);
		EXPECT_EQ(w.src, e.src) << w.blend;
		EXPECT_EQ(w.dst, e.dst) << w.blend;
		EXPECT_EQ(w.op, e.op) << w.blend;
	}
	EXPECT_EQ(kWpBlendFuzz, ComputeDecalPassBlend(true, false));
	EXPECT_EQ(kWpBlendAdd, ComputeDecalPassBlend(false, true));
	EXPECT_EQ(kWpBlendAlpha, ComputeDecalPassBlend(false, false));
}

// [rc4l] A blended HUD model writes depth in every blend, as GL's HUD step leaves glDepthMask on.
TEST(WorldPipelines, AHudModelTakesItsDepthWritingBlend)
{
	EXPECT_EQ(kWpTransHud, ComputeDynamicPipeline(1, false, false, true));
	EXPECT_EQ(kWpAddHud, ComputeDynamicPipeline(2, false, false, true));
	EXPECT_EQ(kWpFuzzHud, ComputeDynamicPipeline(3, false, false, true));
	EXPECT_EQ(kWpRevSubHud, ComputeDynamicPipeline(4, false, false, true));
	EXPECT_EQ(kWpSubHud, ComputeDynamicPipeline(5, false, false, true));
	EXPECT_EQ(kWpTransHud, ComputeDynamicPipeline(-1, false, false, true));
}
