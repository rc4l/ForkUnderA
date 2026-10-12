// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/spritelight_compute.h"

#include <gtest/gtest.h>

using namespace zx::sprites;

TEST(SpriteLight, AGlowingFloorBrightensWhatStandsNearIt)
{
	EXPECT_EQ(ComputeSpriteGlow(100, 0, 64), (255 + 100) >> 1);            // [rc4l] on the floor: the full glow
	EXPECT_EQ(ComputeSpriteGlow(100, -65536, 64), (255 + 100) >> 1);       // [rc4l] below it counts as on it
	EXPECT_EQ(ComputeSpriteGlow(100, 32 * 65536, 64), (32768 * 100 + 177 * 32768) >> 16);   // [rc4l] halfway up: half the glow
	EXPECT_EQ(ComputeSpriteGlow(100, 64 * 65536, 64), 100);                // [rc4l] at the glow's height: none
	EXPECT_EQ(ComputeSpriteGlow(100, 0, 0), 100);                          // [rc4l] a texture with no height does not glow
}

TEST(SpriteLight, ALightAddsByDistanceAndASubtractiveOneTakesAway)
{
	float out[3] = { 0.f, 0.f, 0.f };
	ComputeSpriteLightTerm(50.f, 100.f, 1.f, 0.5f, 0.f, false, 0.f, out);
	EXPECT_EQ(out[0], 0.5f);
	EXPECT_EQ(out[1], 0.25f);
	EXPECT_EQ(out[2], 0.f);
	ComputeSpriteLightTerm(150.f, 100.f, 1.f, 1.f, 1.f, false, 0.f, out);   // [rc4l] out of reach
	ComputeSpriteLightTerm(100.f, 100.f, 1.f, 1.f, 1.f, false, 0.f, out);   // [rc4l] at the edge: nothing
	EXPECT_EQ(out[0], 0.5f);
	float sub[3] = { 0.f, 0.f, 0.f };
	ComputeSpriteLightTerm(0.f, 100.f, 1.f, 0.f, 0.f, true, 1.f, sub);
	EXPECT_EQ(sub[0], 0.f);
	EXPECT_EQ(sub[1], -1.f);
	EXPECT_EQ(sub[2], -1.f);
}

TEST(SpriteLight, ColoursForGreyAndFullbrightSprites)
{
	int r = 30, g = 60, b = 90;
	ComputeDecolorize(r, g, b);
	EXPECT_EQ(r, (255 + 60 + 60) / 3);
	EXPECT_EQ(g, r);
	EXPECT_EQ(b, r);
	r = 0; g = 100; b = 200;
	ComputeFullbrightColor(false, r, g, b);
	EXPECT_EQ(r, 255); EXPECT_EQ(g, 255); EXPECT_EQ(b, 255);
	r = 0; g = 100; b = 200;
	ComputeFullbrightColor(true, r, g, b);
	EXPECT_EQ(r, 0xff / 4); EXPECT_EQ(g, (300 + 0xff) / 4); EXPECT_EQ(b, (600 + 0xff) / 4);
}

TEST(SpriteLight, ALightListCutsASpriteIntoBands)
{
	// [rc4l] A sprite from 64 down to 0 under two light bands, the lower starting at 32.
	const float bottoms[2] = { 32.f, -10.f };
	SpriteBand bands[2];
	const int n = ComputeSpriteBands(64.f, 0.f, 0.f, 1.f, bottoms, 2, bands);
	ASSERT_EQ(n, 2);
	EXPECT_EQ(bands[0].entry, 0);
	EXPECT_EQ(bands[0].z1, 64.f); EXPECT_EQ(bands[0].z2, 32.f);
	EXPECT_EQ(bands[0].vt, 0.f);  EXPECT_EQ(bands[0].vb, 0.5f);
	EXPECT_EQ(bands[1].entry, 1);
	EXPECT_EQ(bands[1].z1, 32.f); EXPECT_EQ(bands[1].z2, 0.f);   // [rc4l] clamped to the sprite's own bottom
	EXPECT_EQ(bands[1].vt, 0.5f); EXPECT_EQ(bands[1].vb, 1.f);
	// [rc4l] A band wholly above the sprite draws nothing.
	const float above[1] = { 80.f };
	EXPECT_EQ(ComputeSpriteBands(64.f, 0.f, 0.f, 1.f, above, 1, bands), 0);
}

TEST(SpriteLight, AnXYBillboardTakesTheBandHoldingItsCentre)
{
	const float bottoms[2] = { 32.f, -10.f };
	EXPECT_EQ(ComputeSpriteColorEntry(0.f, 40.f, bottoms, 2), 0);
	EXPECT_EQ(ComputeSpriteColorEntry(0.f, 20.f, bottoms, 2), 1);
	EXPECT_EQ(ComputeSpriteColorEntry(0.f, -20.f, bottoms, 2), -1);
}
