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

// [rc4l] AOW03's translucent pass re-checked every slot of the bindless array at each pipeline switch, a third of a standalone frame.
TEST(SceneBinding, ALoopTransitionsEachBindingOnlyTheFirstTimeItIsCommitted)
{
	int a, b;
	LoopCommits loop = ComputeLoopCommitsStart();
	EXPECT_TRUE(ComputeCommitTransitions(loop, &a));
	EXPECT_FALSE(ComputeCommitTransitions(loop, &a));
	EXPECT_TRUE(ComputeCommitTransitions(loop, &b));
	EXPECT_FALSE(ComputeCommitTransitions(loop, &b));
	EXPECT_FALSE(ComputeCommitTransitions(loop, &a));
	EXPECT_EQ(2, loop.count);
}

TEST(SceneBinding, ABindingPastAFullRecordIsAlwaysTransitioned)
{
	int slots[kLoopCommitsMax + 1];
	LoopCommits loop = ComputeLoopCommitsStart();
	for (int i = 0; i < kLoopCommitsMax; i++) EXPECT_TRUE(ComputeCommitTransitions(loop, &slots[i]));
	EXPECT_TRUE(ComputeCommitTransitions(loop, &slots[kLoopCommitsMax]));
	EXPECT_TRUE(ComputeCommitTransitions(loop, &slots[kLoopCommitsMax]));
	EXPECT_FALSE(ComputeCommitTransitions(loop, &slots[0]));
	EXPECT_EQ(kLoopCommitsMax, loop.count);
}

// [rc4l] A minimised Vulkan window is a 1x1 swapchain, so minimising and restoring rebuilds twice, and the bindings that dropped must come back.
TEST(SceneBinding, MinimisingAndRestoringRebuildsBothWays)
{
	EXPECT_TRUE(ComputeSceneTargetsRebuild(true, 1024, 640, 1, 1));
	EXPECT_TRUE(ComputeSceneTargetsRebuild(true, 1, 1, 1024, 640));
}
