// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/udmf-desaturation/computation/udmfdesaturation_compute.h"

using namespace zx::udmf;

// [rc4l] WDI09's sector 655 says 99 for nearly grey, and GL mixed 99 times past grey into bright blue.
TEST(UdmfDesaturationCompute, AFractionScalesAndAnythingPastTheEndsIsHeld)
{
	EXPECT_EQ(0, ComputeUdmfDesaturation(0.0));
	EXPECT_EQ(127, ComputeUdmfDesaturation(0.5));     // [rc4l] truncated, as the loader always has
	EXPECT_EQ(252, ComputeUdmfDesaturation(0.99));
	EXPECT_EQ(255, ComputeUdmfDesaturation(1.0));
	EXPECT_EQ(255, ComputeUdmfDesaturation(99.0));
	EXPECT_EQ(0, ComputeUdmfDesaturation(-0.5));
}
