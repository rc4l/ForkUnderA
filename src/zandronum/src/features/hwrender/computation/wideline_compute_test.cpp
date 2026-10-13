// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/wideline_compute.h"

using namespace zx::hwrender;

// [rc4l] Two triangles, each corner once per end and side, both ends on both sides.
TEST(WideLine, TheSixVerticesCoverBothEndsOnBothSides)
{
	int seen[2][2] = { { 0, 0 }, { 0, 0 } };
	for (int k = 0; k < 6; k++)
	{
		float at, side;
		ComputeWideLineVertex(k, at, side);
		seen[at > 0.5f ? 1 : 0][side > 0.f ? 1 : 0]++;
	}
	EXPECT_EQ(2, seen[0][0]); EXPECT_EQ(1, seen[0][1]); EXPECT_EQ(1, seen[1][0]); EXPECT_EQ(2, seen[1][1]);
}

// [rc4l] A horizontal line across a 200x100 screen at w 2: a 4 pixel width moves each corner 2 pixels up or down, 0.04 in NDC and so 0.08 in clip space at that w.
TEST(WideLine, ACornerMovesHalfTheWidthAcrossTheLineInPixels)
{
	const float a[4] = { -1.f, 0.f, 0.5f, 2.f }, b[4] = { 1.f, 0.f, 0.5f, 2.f };
	float out[4];
	ASSERT_TRUE(ComputeWideLineCorner(a, b, 0.f, 1.f, 4.f, 200.f, 100.f, out));
	EXPECT_FLOAT_EQ(out[0], -1.f);
	EXPECT_FLOAT_EQ(out[1], 0.08f);
	EXPECT_FLOAT_EQ(out[2], 0.5f);
	EXPECT_FLOAT_EQ(out[3], 2.f);
	ASSERT_TRUE(ComputeWideLineCorner(a, b, 1.f, -1.f, 4.f, 200.f, 100.f, out));
	EXPECT_FLOAT_EQ(out[0], 1.f);
	EXPECT_FLOAT_EQ(out[1], -0.08f);
	// [rc4l] A vertical line moves its corners across x instead.
	const float c[4] = { 0.f, -1.f, 0.f, 1.f }, d[4] = { 0.f, 1.f, 0.f, 1.f };
	ASSERT_TRUE(ComputeWideLineCorner(c, d, 0.f, 1.f, 2.f, 200.f, 100.f, out));
	EXPECT_FLOAT_EQ(out[0], -0.01f);
	EXPECT_FLOAT_EQ(out[1], -1.f);
}

// [rc4l] An end behind the eye is pulled along the line to the near w, and a line wholly behind it is not drawn.
TEST(WideLine, AnEndBehindTheEyeIsPulledUpToTheNearPlane)
{
	const float a[4] = { 0.f, 0.f, 0.f, -1.f }, b[4] = { 1.f, 0.f, 0.f, 1.f };
	float out[4];
	ASSERT_TRUE(ComputeWideLineCorner(a, b, 0.f, 0.f, 2.f, 100.f, 100.f, out));
	EXPECT_NEAR(out[3], kWideLineNearW, 1e-6f);
	EXPECT_NEAR(out[0], 0.50005f, 1e-6f);
	ASSERT_TRUE(ComputeWideLineCorner(b, a, 1.f, 0.f, 2.f, 100.f, 100.f, out));
	EXPECT_NEAR(out[3], kWideLineNearW, 1e-6f);
	const float behind[4] = { 0.f, 0.f, 0.f, -2.f };
	EXPECT_FALSE(ComputeWideLineCorner(a, behind, 0.f, 1.f, 2.f, 100.f, 100.f, out));
}

// [rc4l] A line seen end-on has no direction on screen, so its corners spread across y rather than dividing by zero.
TEST(WideLine, ALineSeenEndOnStillHasAWidth)
{
	const float a[4] = { 0.f, 0.f, 0.2f, 1.f }, b[4] = { 0.f, 0.f, 0.4f, 2.f };
	float out[4];
	ASSERT_TRUE(ComputeWideLineCorner(a, b, 0.f, 1.f, 2.f, 100.f, 100.f, out));
	EXPECT_FLOAT_EQ(out[0], 0.f);
	EXPECT_FLOAT_EQ(out[1], 0.02f);
}
