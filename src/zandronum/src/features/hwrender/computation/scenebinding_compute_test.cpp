// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/scenebinding_compute.h"

using namespace zx::hwrender;

TEST(SceneBinding, ABatchDrawsWithThePipelineItsBlendNames)
{
	EXPECT_EQ(kBatchPipelineMasked, ComputeBatchPipeline(0));
	EXPECT_EQ(kBatchPipelineAdd, ComputeBatchPipeline(2));
	EXPECT_EQ(kBatchPipelineTrans, ComputeBatchPipeline(1));
	EXPECT_EQ(kBatchPipelineTrans, ComputeBatchPipeline(3));
}

TEST(SceneBinding, TargetsAreBuiltOnceAndRebuiltOnlyForANewSize)
{
	EXPECT_TRUE(ComputeSceneTargetsRebuild(false, 0, 0, 1280, 720));
	EXPECT_FALSE(ComputeSceneTargetsRebuild(true, 1280, 720, 1280, 720));
	EXPECT_TRUE(ComputeSceneTargetsRebuild(true, 1280, 720, 1920, 720));
	EXPECT_TRUE(ComputeSceneTargetsRebuild(true, 1280, 720, 1280, 1080));
}

// [rc4l] A minimised Vulkan window is a 1x1 swapchain, so minimising and restoring rebuilds twice, and the bindings that dropped must come back.
TEST(SceneBinding, MinimisingAndRestoringRebuildsBothWays)
{
	EXPECT_TRUE(ComputeSceneTargetsRebuild(true, 1024, 640, 1, 1));
	EXPECT_TRUE(ComputeSceneTargetsRebuild(true, 1, 1, 1024, 640));
}
