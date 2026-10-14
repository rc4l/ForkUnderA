// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/fuzz_compute.h"

using namespace zx::hwrender;

namespace {

// [rc4l] A white opaque texel through `kind` at one coordinate and time, the values worked out from the GLSL by hand.
void Fuzzed(int kind, float rgba[4])
{
	rgba[0] = rgba[1] = rgba[2] = rgba[3] = 1.f;
	ComputeFuzzTexel(kind, 0.2537f, 0.5013f, 1.25f, rgba);
}

} // namespace

TEST(Fuzz, OnlyAFuzzShaderStyleTakesTheKindAndOnlyAKnownOne)
{
	EXPECT_EQ(kFuzzSwirly, ComputeFuzzKind(3, true));
	EXPECT_EQ(kFuzzNone, ComputeFuzzKind(3, false));
	EXPECT_EQ(kFuzzNone, ComputeFuzzKind(0, true));
	EXPECT_EQ(kFuzzNone, ComputeFuzzKind(kFuzzKinds, true));
}

// [rc4l] Each kind's flicker, against the shader's own arithmetic: snapped to 1/128 for standard and noise, the swirly three sharing one pattern.
TEST(Fuzz, EachKindFlickersTheAlphaAsItsShaderDoes)
{
	const float expected[kFuzzKinds] = { 1.f, 0.08908f, 0.452f, 0.16181f, 0.16181f, 0.16181f, 0.31427f, 0.19168f };
	for (int kind = kFuzzStandard; kind < kFuzzKinds; kind++)
	{
		float c[4];
		Fuzzed(kind, c);
		EXPECT_NEAR(expected[kind], c[3], 2e-3f) << kind;
	}
}

// [rc4l] Every kind draws black but the translucent and jagged ones, which keep the texel's colour; none leaves the texel alone.
TEST(Fuzz, MostKindsDrawBlack)
{
	for (int kind = kFuzzStandard; kind < kFuzzKinds; kind++)
	{
		float c[4];
		Fuzzed(kind, c);
		const bool keeps = kind == kFuzzTranslucent || kind == kFuzzJagged;
		EXPECT_EQ(keeps ? 1.f : 0.f, c[0]) << kind;
	}
	float c[4];
	Fuzzed(kFuzzNone, c);
	EXPECT_EQ(1.f, c[0]);
	EXPECT_EQ(1.f, c[3]);
}

TEST(Fuzz, TheJaggedKindSamplesASplatOfItsCoordinate)
{
	float su, sv;
	ComputeFuzzSplat(0.2537f, 0.5013f, 1.25f, su, sv);
	EXPECT_NEAR(0.25452f, su, 1e-4f);
	EXPECT_NEAR(0.50362f, sv, 1e-4f);
}
