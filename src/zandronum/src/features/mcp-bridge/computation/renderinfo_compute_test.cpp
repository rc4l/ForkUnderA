// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/mcp-bridge/computation/renderinfo_compute.h"

using namespace zx::mcp;

// [rc4l] The mac crash: a Vulkan-alone launch keeps currentrenderer at 1, and used to be dumped as GL.
TEST(RenderInfo, BackendOnlyIsVulkanWhateverCurrentrendererSays)
{
	EXPECT_STREQ(ComputeRendererName(true, 1), "vulkan");
	EXPECT_STREQ(ComputeRendererName(true, 0), "vulkan");
}

TEST(RenderInfo, WithoutTheBackendCurrentrendererNamesTheRenderer)
{
	EXPECT_STREQ(ComputeRendererName(false, 1), "opengl");
	EXPECT_STREQ(ComputeRendererName(false, 0), "software");
}

TEST(RenderInfo, GlIsLiveOnlyForAGlLaunchThatIsNotBackendOnly)
{
	EXPECT_TRUE(ComputeGlLive(1, false));
	EXPECT_FALSE(ComputeGlLive(1, true));
	EXPECT_FALSE(ComputeGlLive(0, false));
	EXPECT_FALSE(ComputeGlLive(0, true));
}
