// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/hwrender/computation/resourcestate_compute.h"

#include <vector>

using namespace zx::hwrender;

// [Doruk-Aksoy] Each read binding maps to its read state, and a buffer bound for several reads goes back to all of them at once.
TEST(ResourceState, AWrittenBufferGoesBackToEveryReadItIsBoundFor)
{
	EXPECT_EQ(kStateVertexBuffer, ComputeBufferReadState(kBindVertexBuffer));
	EXPECT_EQ(kStateIndexBuffer, ComputeBufferReadState(kBindIndexBuffer));
	EXPECT_EQ(kStateConstantBuffer, ComputeBufferReadState(kBindUniformBuffer));
	EXPECT_EQ(kStateShaderResource, ComputeBufferReadState(kBindShaderResource));
	EXPECT_EQ(kStateIndirectArgument, ComputeBufferReadState(kBindIndirectDrawArgs));
	EXPECT_EQ(kStateVertexBuffer | kStateShaderResource, ComputeBufferReadState(kBindVertexBuffer | kBindShaderResource));
}

// [Doruk-Aksoy] A write binding or a ray-tracing build is no read, so a buffer bound only for those has no state to go back to.
TEST(ResourceState, WritesAndRayTracingBuildsAreNoReadState)
{
	EXPECT_EQ(0u, ComputeBufferReadState(kBindUnorderedAccess));
	EXPECT_EQ(0u, ComputeBufferReadState(kBindRayTracing));
	EXPECT_EQ(0u, ComputeBufferReadState(0));
	EXPECT_EQ(kStateShaderResource, ComputeBufferReadState(kBindShaderResource | kBindUnorderedAccess | kBindRayTracing));
}

// [Doruk-Aksoy] A resource written several times before the next draw is made ready once.
TEST(ResourceState, AResourceIsNotedOnce)
{
	std::vector<const void *> list;
	int a = 0, b = 0;
	EXPECT_TRUE(ComputeNoteOnce(list, &a));
	EXPECT_TRUE(ComputeNoteOnce(list, &b));
	EXPECT_FALSE(ComputeNoteOnce(list, &a));
	EXPECT_EQ(2u, list.size());
}
