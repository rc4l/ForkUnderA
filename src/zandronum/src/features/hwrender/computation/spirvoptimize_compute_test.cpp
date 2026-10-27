// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/hwrender/computation/spirvoptimize_compute.h"

using namespace zx::hwrender;

// [Doruk-Aksoy] The default leaves Vulkan's SPIR-V as glslang made it and optimizes WebGPU's.
TEST(SpirvOptimize, TheDefaultOptimizesOnlyWebGPU)
{
	EXPECT_FALSE(ComputeOptimizeSpirv(-1, false));
	EXPECT_TRUE(ComputeOptimizeSpirv(-1, true));
	EXPECT_FALSE(ComputeOptimizeSpirv(7, false));
	EXPECT_TRUE(ComputeOptimizeSpirv(7, true));
}

// [Doruk-Aksoy] 1 and 0 hold on either device.
TEST(SpirvOptimize, OneAndZeroHoldEverywhere)
{
	EXPECT_TRUE(ComputeOptimizeSpirv(1, false));
	EXPECT_TRUE(ComputeOptimizeSpirv(1, true));
	EXPECT_FALSE(ComputeOptimizeSpirv(0, false));
	EXPECT_FALSE(ComputeOptimizeSpirv(0, true));
}
