// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <math.h>

#include "features/hwrender/computation/warp_compute.h"

using namespace zx::hwrender;

// [rc4l] Only ANIMDEFS warp (1) and warp2 (2) ripple; a plain texture, or any other value, is left still.
TEST(Warp, StyleIsTheTexturesWarpOrNone)
{
	EXPECT_EQ(0, ComputeWarpStyle(0));
	EXPECT_EQ(1, ComputeWarpStyle(1));
	EXPECT_EQ(2, ComputeWarpStyle(2));
	EXPECT_EQ(0, ComputeWarpStyle(3));
	EXPECT_EQ(0, ComputeWarpStyle(-1));
}

// [rc4l] GL's warp clock is gl_frameMS * speed / 1000, so an hour in at double speed reads 7200.
TEST(Warp, TimerIsSecondsTimesSpeed)
{
	EXPECT_FLOAT_EQ(3.f, ComputeWarpTimer(2000u, 1.5f));
	EXPECT_FLOAT_EQ(7200.f, ComputeWarpTimer(3600000u, 2.f));
}

TEST(Warp, AStillTextureHasNoOffset)
{
	float du = 1.f, dv = 1.f;
	ComputeWarpOffset(0, 0.25f, 0.5f, 10.f, &du, &dv);
	EXPECT_FLOAT_EQ(0.f, du);
	EXPECT_FLOAT_EQ(0.f, dv);
}

// [rc4l] func_warp1.fp: a 0.1 sine across each axis, one cycle per texture, eight seconds to come round.
TEST(Warp, WarpOneMovesByTheOtherAxisAndTime)
{
	float du, dv;
	ComputeWarpOffset(1, 0.25f, 0.f, 0.f, &du, &dv);
	EXPECT_NEAR(0.f, du, 1e-6f);
	EXPECT_NEAR(0.1f, dv, 1e-6f);
	ComputeWarpOffset(1, 0.f, 0.f, 2.f, &du, &dv);
	EXPECT_NEAR(0.1f, du, 1e-6f);
	EXPECT_NEAR(0.1f, dv, 1e-6f);
}

// [rc4l] func_warp2.fp: a shared 0.03 sine down the texture plus 0.03 and 0.02 across it, on 0.75 and 0.45 clocks.
TEST(Warp, WarpTwoAddsTheSharedRippleToBothAxes)
{
	float du, dv;
	ComputeWarpOffset(2, 0.f, 0.f, 0.f, &du, &dv);
	EXPECT_NEAR(0.f, du, 1e-6f);
	EXPECT_NEAR(0.f, dv, 1e-6f);
	const float t = 1.f / 3.f;
	ComputeWarpOffset(2, 0.f, 0.f, t, &du, &dv);
	EXPECT_NEAR(0.06f, dv, 1e-5f);
	EXPECT_NEAR(0.03f + sinf(3.14159265f * 2.f * t * 0.45f) * 0.02f, du, 1e-5f);
}
