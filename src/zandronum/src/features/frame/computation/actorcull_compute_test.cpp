// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/actorcull_compute.h"

#include <gtest/gtest.h>

using namespace zx::frame;

namespace {

// [rc4l] A projection looking down -z with w = -z, x and y passed through: what is in front has positive w.
void LookDownZ(float *m)
{
	for (int i = 0; i < 16; i++) m[i] = 0.f;
	m[0] = 1.f; m[5] = 1.f; m[10] = 1.f; m[11] = -1.f;
}

} // namespace

TEST(ActorCull, InFrontAndOnScreenIsInView)
{
	float m[16];
	LookDownZ(m);
	EXPECT_EQ(ComputeActorCull(m, 0.f, 0.f, -100.f, 10.f), kActorInView);
}

TEST(ActorCull, FarBehindTheEyeIsBehindButJustBehindIsKept)
{
	float m[16];
	LookDownZ(m);
	EXPECT_EQ(ComputeActorCull(m, 0.f, 0.f, 100.f, 10.f), kActorBehind);
	EXPECT_EQ(ComputeActorCull(m, 0.f, 0.f, 5.f, 10.f), kActorInView);   // [rc4l] within its pad of the eye
}

TEST(ActorCull, PastEitherSideIsOffScreen)
{
	float m[16];
	LookDownZ(m);
	EXPECT_EQ(ComputeActorCull(m, 1000.f, 0.f, -100.f, 10.f), kActorOffScreen);
	EXPECT_EQ(ComputeActorCull(m, 0.f, -1000.f, -100.f, 10.f), kActorOffScreen);
	EXPECT_EQ(ComputeActorCull(m, 120.f, 0.f, -100.f, 10.f), kActorInView);   // [rc4l] inside the padded edge
}
