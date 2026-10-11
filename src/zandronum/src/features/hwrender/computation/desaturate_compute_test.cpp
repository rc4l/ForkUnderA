// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/hwrender/computation/desaturate_compute.h"

using namespace zx::hwrender;

// [rc4l] WDI09's sector 655 asks for a grey corner, which the backend drew in full colour having never ported desaturation.
TEST(DesaturateCompute, AFullFactorIsTheGreyAndNoneLeavesTheColour)
{
	float wood[3] = { 0.5f, 0.3f, 0.1f };
	ComputeDesaturate(wood, 1.f);
	const float grey = 0.5f * 0.3f + 0.3f * 0.56f + 0.1f * 0.14f;
	for (int c = 0; c < 3; c++) EXPECT_FLOAT_EQ(grey, wood[c]) << c;
	float half[3] = { 1.f, 0.f, 0.f };
	ComputeDesaturate(half, 0.5f);
	EXPECT_FLOAT_EQ(0.65f, half[0]);
	EXPECT_FLOAT_EQ(0.15f, half[1]);
	float kept[3] = { 0.2f, 0.4f, 0.6f };
	ComputeDesaturate(kept, 0.f);
	EXPECT_FLOAT_EQ(0.2f, kept[0]);
	EXPECT_FLOAT_EQ(0.6f, kept[2]);
	EXPECT_FLOAT_EQ(1.f, ComputeDesaturationFactor(255));
	EXPECT_FLOAT_EQ(0.f, ComputeDesaturationFactor(0));
}
