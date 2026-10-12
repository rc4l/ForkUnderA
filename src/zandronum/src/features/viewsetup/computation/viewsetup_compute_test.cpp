// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/viewsetup/computation/viewsetup_compute.h"

#include <gtest/gtest.h>

using namespace zx::viewsetup;

TEST(ViewSetup, PitchStretchesWithThePixelRatio)
{
	EXPECT_FLOAT_EQ(ComputeStretchedPitch(0, 1.2f), 0.f);
	// [rc4l] 30 degrees down at square pixels stays 30, and steepens once each pixel is 1.2 tall.
	const unsigned down30 = 0x15555555u;
	EXPECT_NEAR(ComputeStretchedPitch(down30, 1.f), 30.f, 1e-4);
	EXPECT_NEAR(ComputeStretchedPitch(down30, 1.2f), 34.7150f, 1e-3);
	EXPECT_NEAR(ComputeStretchedPitch(0u - down30, 1.f), -30.f, 1e-4);   // [rc4l] up wraps past half a turn to negative
}

TEST(ViewSetup, PitchIsClampedToStraightUpAndDown)
{
	EXPECT_FLOAT_EQ(ComputeStretchedPitch(0x50000000u, 1.f), 90.f);   // [rc4l] past straight down
	EXPECT_FLOAT_EQ(ComputeStretchedPitch(0xB0000000u, 1.f), -90.f);   // [rc4l] past straight up
}

TEST(ViewSetup, TheFrustumWidensWithTiltAndOpensFullyPastIt)
{
	const unsigned level = ComputeFrustumAngle(0.f, 90.f, 48);   // [rc4l] 4:3 at 90 degrees: 47 degrees either side
	EXPECT_EQ(level, 0x216C16C1u);
	EXPECT_GT(ComputeFrustumAngle(-20.f, 90.f, 48), level);
	EXPECT_EQ(ComputeFrustumAngle(47.f, 90.f, 48), 0xffffffffu);
	EXPECT_EQ(ComputeFrustumAngle(10.f, 400.f, 48), 0xffffffffu);  // [rc4l] a lens wide enough to pass half a turn sees everything
	EXPECT_LT(ComputeFrustumAngle(0.f, 90.f, 64), level);          // [rc4l] a wider aspect's multiplier narrows it
}
