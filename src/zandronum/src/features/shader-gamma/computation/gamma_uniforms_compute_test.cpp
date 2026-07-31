// [rc4l] Tests for the gamma/contrast/brightness -> shader-uniform conversion.
//
// These clamps are what stand between a bad cvar and an unreadable screen, and the user cannot
// easily fix a black screen from inside a black screen -- so the bounds and the NaN fallbacks are
// pinned rather than left to inspection.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"
#include "features/shader-gamma/computation/gamma_uniforms_compute.h"

using namespace zx;

TEST(GammaUniforms, DefaultsAreNeutral)
{
	// Gamma 1 / contrast 1 / brightness 0 are the shipped cvar defaults and must leave the image
	// untouched: pow(x,1) == x, and the contrast/brightness terms cancel.
	const GammaUniforms u = ComputeGammaUniforms(1.0f, 1.0f, 0.0f);
	EXPECT_FLOAT_EQ(u.invGamma, 1.0f);
	EXPECT_FLOAT_EQ(u.contrast, 1.0f);
	EXPECT_FLOAT_EQ(u.brightness, 0.0f);
	EXPECT_TRUE(GammaUniformsAreNeutral(u));
}

TEST(GammaUniforms, GammaIsInvertedForTheShader)
{
	// The shader does pow(val, InvGamma), so a gamma of 2 must arrive as 0.5.
	EXPECT_FLOAT_EQ(ComputeGammaUniforms(2.0f, 1.0f, 0.0f).invGamma, 0.5f);
	EXPECT_FLOAT_EQ(ComputeGammaUniforms(4.0f, 1.0f, 0.0f).invGamma, 0.25f);
}

TEST(GammaUniforms, BoundsMatchTheOldHardwareRampClamps)
{
	// gamma 0.1..4 -- the low bound is also what keeps 1/g finite.
	EXPECT_FLOAT_EQ(ComputeGammaUniforms(0.0f, 1.0f, 0.0f).invGamma, 10.0f);   // clamped to 0.1
	EXPECT_FLOAT_EQ(ComputeGammaUniforms(-5.0f, 1.0f, 0.0f).invGamma, 10.0f);
	EXPECT_FLOAT_EQ(ComputeGammaUniforms(99.0f, 1.0f, 0.0f).invGamma, 0.25f);  // clamped to 4

	// contrast 0.1..3
	EXPECT_FLOAT_EQ(ComputeGammaUniforms(1.0f, -1.0f, 0.0f).contrast, 0.1f);
	EXPECT_FLOAT_EQ(ComputeGammaUniforms(1.0f, 99.0f, 0.0f).contrast, 3.0f);

	// brightness -0.8..0.8
	EXPECT_FLOAT_EQ(ComputeGammaUniforms(1.0f, 1.0f, -9.0f).brightness, -0.8f);
	EXPECT_FLOAT_EQ(ComputeGammaUniforms(1.0f, 1.0f, 9.0f).brightness, 0.8f);
}

TEST(GammaUniforms, ValuesInsideTheBoundsPassThroughUnchanged)
{
	const GammaUniforms u = ComputeGammaUniforms(0.5f, 1.5f, -0.25f);
	EXPECT_FLOAT_EQ(u.invGamma, 2.0f);
	EXPECT_FLOAT_EQ(u.contrast, 1.5f);
	EXPECT_FLOAT_EQ(u.brightness, -0.25f);
	EXPECT_FALSE(GammaUniformsAreNeutral(u));
}

TEST(GammaUniforms, NaNFallsBackToNeutralRatherThanReachingTheShader)
{
	// A NaN uniform blanks the frame, and a blank frame is not something the user can navigate to
	// fix. Each field falls back independently to its neutral value.
	const float nan = 0.0f / 0.0f;
	const GammaUniforms g = ComputeGammaUniforms(nan, 1.0f, 0.0f);
	EXPECT_FLOAT_EQ(g.invGamma, 1.0f);
	const GammaUniforms c = ComputeGammaUniforms(1.0f, nan, 0.0f);
	EXPECT_FLOAT_EQ(c.contrast, 1.0f);
	const GammaUniforms b = ComputeGammaUniforms(1.0f, 1.0f, nan);
	EXPECT_FLOAT_EQ(b.brightness, 0.0f);
	// All three at once still lands on a fully neutral, safe result.
	EXPECT_TRUE(GammaUniformsAreNeutral(ComputeGammaUniforms(nan, nan, nan)));
}

TEST(GammaUniforms, NeutralPredicateRejectsAnyNonNeutralField)
{
	EXPECT_FALSE(GammaUniformsAreNeutral(ComputeGammaUniforms(2.0f, 1.0f, 0.0f)));
	EXPECT_FALSE(GammaUniformsAreNeutral(ComputeGammaUniforms(1.0f, 1.5f, 0.0f)));
	EXPECT_FALSE(GammaUniformsAreNeutral(ComputeGammaUniforms(1.0f, 1.0f, 0.3f)));
}
