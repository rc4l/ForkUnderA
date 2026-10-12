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

TEST(ViewSetup, TheSkyScrollsWithTheClockAndWraps)
{
	EXPECT_FLOAT_EQ(ComputeSkyScroll(0, 0.5f), 0.f);
	EXPECT_FLOAT_EQ(ComputeSkyScroll(512, 1.f), 512.f * 90.f / 256.f);
	EXPECT_FLOAT_EQ(ComputeSkyScroll(1536, 1.f), 512.f * 90.f / 256.f);   // [rc4l] a wrapped clock scrolls as far
}

TEST(ViewSetup, TheViewAngleGivesTheYawAndFacing)
{
	float yaw, x, y;
	ComputeViewAngleVector(0, yaw, x, y);
	EXPECT_FLOAT_EQ(yaw, 270.f); EXPECT_FLOAT_EQ(x, 1.f); EXPECT_NEAR(y, 0.f, 1e-6f);
	ComputeViewAngleVector(0x40000000u, yaw, x, y);   // [rc4l] a quarter turn: north
	EXPECT_FLOAT_EQ(yaw, 180.f); EXPECT_NEAR(x, 0.f, 1e-6f); EXPECT_FLOAT_EQ(y, 1.f);
}

TEST(ViewSetup, TheViewAreaAgainstAHeightTransfer)
{
	EXPECT_EQ(ComputeViewArea(false, 0, 0, 0, false), kAreaDefault);
	EXPECT_EQ(ComputeViewArea(true, 10, 10, 100, false), kAreaBelow);
	EXPECT_EQ(ComputeViewArea(true, 50, 10, 100, false), kAreaNormal);
	EXPECT_EQ(ComputeViewArea(true, 150, 10, 100, false), kAreaAbove);
	EXPECT_EQ(ComputeViewArea(true, 150, 10, 100, true), kAreaNormal);   // [rc4l] a floor-only transfer has no above
}

TEST(ViewSetup, TheEyeStaysFourUnitsInside)
{
	const long long u = 1 << 16;
	EXPECT_EQ(ComputeClampedViewZ(50 * u, 0, 100 * u), 50 * u);
	EXPECT_EQ(ComputeClampedViewZ(99 * u, 0, 100 * u), 96 * u);
	EXPECT_EQ(ComputeClampedViewZ(1 * u, 0, 100 * u), 4 * u);
	EXPECT_EQ(ComputeClampedViewZ(1 * u, 0, 2 * u), 4 * u);   // [rc4l] crossed: the floor wins
}

TEST(ViewSetup, TheFovRatioIsFourThirdsUnlessTheScreenIsTall)
{
	EXPECT_FLOAT_EQ(ComputeFovRatio(16.f / 9.f, false), 1.333333f);
	EXPECT_FLOAT_EQ(ComputeFovRatio(1.25f, true), 1.25f);
}

TEST(ComputeMaxViewPitch, TheSoftwareLimitsOnlyWhenForced)
{
	EXPECT_EQ(90, zx::viewsetup::ComputeMaxViewPitch(true, false, true, 90));
	EXPECT_EQ(56, zx::viewsetup::ComputeMaxViewPitch(true, true, true, 90));
	EXPECT_EQ(32, zx::viewsetup::ComputeMaxViewPitch(false, true, true, 90));
	EXPECT_EQ(56, zx::viewsetup::ComputeMaxViewPitch(false, true, false, 90));
}

TEST(ComputeFixedColormap, TheInverseMapThenThePlayersThenALightAmplifiers)
{
	using namespace zx::viewsetup;
	FixedColormapIn in = { true, false, kNoFixedColormap, -1, kNoLightPower };
	EXPECT_EQ(kCmDefault, ComputeFixedColormap(in));
	in.lightPower = kTorchPower;
	EXPECT_EQ(kCmDefault, ComputeFixedColormap(in));
	in.fixedLightLevel = 3;
	EXPECT_EQ(3 + kCmTorch, ComputeFixedColormap(in));
	in.lightPower = kLightAmpPower;
	EXPECT_EQ(kCmLite, ComputeFixedColormap(in));
	in.lightPower = kNoLightPower;
	EXPECT_EQ(kCmDefault, ComputeFixedColormap(in));
	in.fixedColormap = 2;
	EXPECT_EQ(kCmFirstSpecial + 2, ComputeFixedColormap(in));
	in.inverse = true;
	EXPECT_EQ(kCmFirstSpecial + kInverseColormap, ComputeFixedColormap(in));
	in.hasPlayer = false;
	EXPECT_EQ(kCmDefault, ComputeFixedColormap(in));
}

TEST(ComputeSeesOwnBody, TheChaseCameraOrADeadPlayersDeathCamera)
{
	using zx::viewsetup::ComputeSeesOwnBody;
	EXPECT_TRUE(ComputeSeesOwnBody(true, false, true, false, true, false, false, true));
	EXPECT_TRUE(ComputeSeesOwnBody(true, false, true, false, false, true, true, true));
	EXPECT_FALSE(ComputeSeesOwnBody(true, false, true, false, false, true, false, true));
	EXPECT_FALSE(ComputeSeesOwnBody(false, false, true, false, true, false, false, true));
	EXPECT_FALSE(ComputeSeesOwnBody(true, true, true, false, true, false, false, true));
	EXPECT_FALSE(ComputeSeesOwnBody(true, false, true, true, true, false, false, true));
	EXPECT_TRUE(ComputeSeesOwnBody(true, false, false, true, true, false, false, true));
	EXPECT_FALSE(ComputeSeesOwnBody(true, false, true, false, true, false, false, false));
}
