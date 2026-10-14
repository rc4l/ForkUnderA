// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/display/computation/display_compute.h"

#include <gtest/gtest.h>

using namespace zx::display;

TEST(Display, TheNeutralRampIsTheIdentityTimes256)
{
	unsigned short ramp[768];
	ComputeGammaRamp(1.f, 1.f, 0.f, ramp);
	EXPECT_EQ(ramp[0], 0);
	EXPECT_EQ(ramp[128], 128 * 256);
	EXPECT_EQ(ramp[255 + 256], 255 * 256);   // [rc4l] the three channels share one curve
	EXPECT_EQ(ramp[100 + 512], 100 * 256);
}

TEST(Display, GammaBendsTheCurveAndTheInputsAreClamped)
{
	unsigned short bright[768], neutral[768], clamped[768], high[768];
	ComputeGammaRamp(2.f, 1.f, 0.f, bright);
	ComputeGammaRamp(1.f, 1.f, 0.f, neutral);
	EXPECT_GT(bright[64], neutral[64]);
	EXPECT_NEAR(bright[255], neutral[255], 1);   // [rc4l] the ends stay put, to a rounding
	ComputeGammaRamp(9.f, 9.f, 9.f, clamped);
	ComputeGammaRamp(4.f, 3.f, 0.8f, high);
	EXPECT_EQ(clamped[50], high[50]);
	EXPECT_EQ(clamped[255], 0xffff);         // [rc4l] saturated
	ComputeGammaRamp(1.f, 1.f, -9.f, clamped);
	EXPECT_EQ(clamped[0], 0);                // [rc4l] cannot go below black
}

// [rc4l] High contrast pushes the low levels below black, which gamma's pow turns to NaN; they come out black, as on Windows, not as whatever casting NaN gives.
TEST(Display, LevelsBelowBlackUnderGammaAreBlack)
{
	unsigned short ramp[768];
	ComputeGammaRamp(2.f, 3.f, 0.f, ramp);
	EXPECT_EQ(ramp[0], 0);
	EXPECT_EQ(ramp[50 + 256], 0);
	EXPECT_EQ(ramp[255], 0xffff);
}

TEST(Display, PaletteBrightnessIsTheWeightedAverage)
{
	unsigned white[256], grey[256];
	for (int i = 0; i < 256; i++) { white[i] = 0xffffffffu; grey[i] = 0xff808080u; }
	EXPECT_EQ(ComputePaletteBrightness(white, 256), 255);
	EXPECT_EQ(ComputePaletteBrightness(grey, 256), 128);
}
