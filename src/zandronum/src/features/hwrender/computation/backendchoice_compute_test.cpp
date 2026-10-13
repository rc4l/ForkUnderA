// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/backendchoice_compute.h"

using namespace zx::hwrender;

// [rc4l] Vulkan is the default and the setting's choice; GL (Legacy) is the player's.
TEST(BackendChoice, TheSettingChoosesVulkanByDefault)
{
	EXPECT_TRUE(ComputeWantsBackendOnly(kVidBackendVulkan, false, false, true));
	EXPECT_FALSE(ComputeWantsBackendOnly(kVidBackendGL, false, false, true));
	// [rc4l] An unknown value from a hand-edited config is not GL's 0, so it is Vulkan.
	EXPECT_TRUE(ComputeWantsBackendOnly(7, false, false, true));
}

// [rc4l] The command line overrides the setting, -gl winning when both are given, as the harness's GL launches need.
TEST(BackendChoice, TheCommandLineOverridesTheSetting)
{
	EXPECT_TRUE(ComputeWantsBackendOnly(kVidBackendGL, true, false, true));
	EXPECT_FALSE(ComputeWantsBackendOnly(kVidBackendVulkan, false, true, true));
	EXPECT_FALSE(ComputeWantsBackendOnly(kVidBackendVulkan, true, true, true));
}

TEST(BackendChoice, ABuildWithoutTheBackendIsAlwaysGL)
{
	EXPECT_FALSE(ComputeWantsBackendOnly(kVidBackendVulkan, true, false, false));
}
