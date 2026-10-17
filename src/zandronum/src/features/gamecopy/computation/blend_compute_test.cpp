// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/computation/blend_compute.h"

#include <gtest/gtest.h>

using zx::Fixed;
using zx::gamecopy::Blends;
using zx::gamecopy::ComputeBlendFixed;

namespace {
Fixed F(int units) { return Fixed::FromRaw((int64_t)units * 65536); }
Fixed Frac(double f) { return Fixed::FromRaw((int64_t)(f * 65536)); }
}

TEST(GameCopyBlend, TheEndsOfTheTicAreItsTwoValues)
{
	EXPECT_EQ(F(10).Raw(), ComputeBlendFixed(F(10), F(30), Frac(0)).Raw());
	EXPECT_EQ(F(30).Raw(), ComputeBlendFixed(F(10), F(30), Frac(1)).Raw());
}

TEST(GameCopyBlend, BetweenThemItMovesAsTheInterpolationsWriteIt)
{
	EXPECT_EQ(F(20).Raw(), ComputeBlendFixed(F(10), F(30), Frac(0.5)).Raw());
	EXPECT_EQ(F(25).Raw(), ComputeBlendFixed(F(30), F(10), Frac(0.25)).Raw());
	const Fixed old = Fixed::FromRaw(123457), cur = Fixed::FromRaw(-987653), sr = Fixed::FromRaw(40001);
	EXPECT_EQ((old + FixedMul(cur - old, sr)).Raw(), ComputeBlendFixed(old, cur, sr).Raw());
}

TEST(GameCopyBlend, TheListEmptiesWhole)
{
	Blends b;
	EXPECT_TRUE(b.Empty());
	b.planes.push_back({ 1, false, F(0), F(1), F(0), F(1) });
	EXPECT_FALSE(b.Empty());
	b.Clear();
	EXPECT_TRUE(b.Empty());
	b.scrolls.push_back({ 2, true, F(0), F(1), F(0), F(1) });
	EXPECT_FALSE(b.Empty());
	b.Clear();
	b.walls.push_back({ 3, 1, F(0), F(1), F(0), F(1) });
	EXPECT_FALSE(b.Empty());
	b.Clear();
	b.polys.push_back({ { 4 }, { F(0), F(0) }, { F(1), F(1) } });
	EXPECT_FALSE(b.Empty());
}
