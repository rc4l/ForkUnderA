// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/gamecopy/computation/cullpoint_compute.h"

#include <gtest/gtest.h>

using zx::gamecopy::ActorCullPoint;
using zx::gamecopy::ComputeActorCullPoint;

TEST(ActorCullPoint, CentreIsMidHeightInMeshOrder)
{
	const ActorCullPoint p = ComputeActorCullPoint(true, 10.f, 20.f, 30.f, 56.f, 20.f, 0.f);
	EXPECT_FLOAT_EQ(p.x, 10.f);
	EXPECT_FLOAT_EQ(p.up, 58.f);
	EXPECT_FLOAT_EQ(p.y, 20.f);
}

TEST(ActorCullPoint, PadTakesTheRadiusWhenTheModelReachesLess)
{
	EXPECT_FLOAT_EQ(ComputeActorCullPoint(true, 0.f, 0.f, 0.f, 56.f, 20.f, 8.f).pad, 20.f + 56.f + 64.f);
}

TEST(ActorCullPoint, PadTakesTheModelReachPastTheRadius)
{
	EXPECT_FLOAT_EQ(ComputeActorCullPoint(true, 0.f, 0.f, 0.f, 16.f, 20.f, 300.f).pad, 300.f + 16.f + 64.f);
}

TEST(ActorCullPoint, NoSectorReadsAsANegativePad)
{
	const ActorCullPoint p = ComputeActorCullPoint(false, 1.f, 2.f, 3.f, 56.f, 20.f, 0.f);
	EXPECT_LT(p.pad, 0.f);
	EXPECT_FLOAT_EQ(p.x, 1.f);
	EXPECT_FLOAT_EQ(p.y, 2.f);
}

TEST(ActorCullPoint, MatchesTheSweepsOwnArithmeticBitForBit)
{
	// [Doruk-Aksoy] The sweep's verdicts must not move, so the point repeats its expressions exactly, odd fractions included.
	const float z = 123.456f, h = 57.3f, r = 19.9f, reach = 3.1f;
	const ActorCullPoint p = ComputeActorCullPoint(true, 0.f, 0.f, z, h, r, reach);
	const float up = z + h * 0.5f, pad = (r > reach ? r : reach) + h + 64.f;
	EXPECT_EQ(p.up, up);
	EXPECT_EQ(p.pad, pad);
}
