// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/spritestyle_compute.h"

#include <gtest/gtest.h>

using namespace zx::sprites;

namespace {

const SpriteStyle kNormal = { kOpAdd, kAlphaSrc, kAlphaInvSrc, 0 };
const SpriteStyle kTranslucent = { kOpAdd, kAlphaSrc, kAlphaInvSrc, 0 };

SpriteStyleIn In(SpriteStyle s, float alpha = 1.f)
{
	SpriteStyleIn in = {};
	in.style = s;
	in.alpha = alpha;
	in.transSouls = 0.66f;
	in.spriteBlend = false;
	in.translucent = kTranslucent;
	return in;
}

} // namespace

TEST(SpriteStyle, AnOpaqueSpriteIsDrawnSolidUnlessSpriteBlendIsOn)
{
	SpriteStyleOut o = ComputeSpriteStyle(In(kNormal));
	EXPECT_EQ(o.trans, 1.f);
	EXPECT_EQ(o.hw, kHwSolid);
	EXPECT_EQ(o.style.srcAlpha, kAlphaOne);
	EXPECT_EQ(o.style.destAlpha, kAlphaZero);
	SpriteStyleIn in = In(kNormal);
	in.spriteBlend = true;
	o = ComputeSpriteStyle(in);
	EXPECT_EQ(o.hw, kHwNormal);
	EXPECT_EQ(o.style.srcAlpha, kAlphaSrc);
	in.model = true;   // [rc4l] a model is always solid
	EXPECT_EQ(ComputeSpriteStyle(in).hw, kHwSolid);
	const SpriteStyle oneZero = { kOpAdd, kAlphaOne, kAlphaZero, 0 };
	EXPECT_EQ(ComputeSpriteStyle(In(oneZero)).hw, kHwSolid);
}

TEST(SpriteStyle, TranslucencyComesFromAlphaTranssoulsOrAlpha1)
{
	SpriteStyleOut o = ComputeSpriteStyle(In(kNormal, 0.5f));
	EXPECT_EQ(o.trans, 0.5f);
	EXPECT_EQ(o.hw, kHwNormal);
	SpriteStyle souls = kNormal; souls.flags = kFlagTransSouls;
	EXPECT_EQ(ComputeSpriteStyle(In(souls, 0.2f)).trans, 0.66f);
	SpriteStyle one = kNormal; one.flags = kFlagAlpha1;
	EXPECT_EQ(ComputeSpriteStyle(In(one, 0.2f)).trans, 1.f);
}

TEST(SpriteStyle, FuzzIsAShadowOrTheFuzzShader)
{
	const SpriteStyle fuzz = { kOpFuzz, kAlphaSrc, kAlphaInvSrc, 0 };
	SpriteStyleOut o = ComputeSpriteStyle(In(fuzz));
	EXPECT_EQ(o.style.blendOp, kOpShadow);
	EXPECT_EQ(o.overrideShader, -1);
	SpriteStyleIn in = In(fuzz);
	in.fuzzType = 2;
	o = ComputeSpriteStyle(in);
	EXPECT_EQ(o.style.blendOp, kOpAdd);
	EXPECT_EQ(o.overrideShader, 6);
	EXPECT_EQ(o.trans, 0.99f);
	EXPECT_EQ(o.hw, kHwNoAlphaTest);
}

TEST(SpriteStyle, TranslucentTexelsOrRedAlphaTurnTheAlphaTestOff)
{
	SpriteStyleIn in = In(kNormal);
	in.textureTransparent = true;
	SpriteStyleOut o = ComputeSpriteStyle(in);
	EXPECT_EQ(o.hw, kHwNoAlphaTest);
	EXPECT_EQ(o.style.srcAlpha, kAlphaSrc);   // [rc4l] a solid one goes back to blending
	EXPECT_EQ(o.style.destAlpha, kAlphaInvSrc);
	SpriteStyle red = { kOpAdd, kAlphaSrc, kAlphaOne, kFlagRedIsAlpha };
	o = ComputeSpriteStyle(In(red, 0.5f));
	EXPECT_EQ(o.hw, kHwNoAlphaTest);
	EXPECT_EQ(o.style.destAlpha, kAlphaOne);
}

TEST(SpriteStyle, EnhancedVisionRevealsShadowsAndStealth)
{
	const SpriteStyle shadow = { kOpShadow, kAlphaSrc, kAlphaInvSrc, 16 };
	SpriteStyleIn in = In(shadow);
	in.enhancedVision = true;
	SpriteStyleOut o = ComputeSpriteStyle(in);
	EXPECT_EQ(o.trans, 0.5f);
	EXPECT_EQ(o.style.blendOp, kOpAdd);
	EXPECT_EQ(o.style.flags, 16);   // [rc4l] only the flags survive
	in = In(kNormal, 0.1f);
	in.enhancedVision = true;
	in.stealth = true;
	EXPECT_EQ(ComputeSpriteStyle(in).trans, 0.5f);
	in.stealth = false;
	EXPECT_EQ(ComputeSpriteStyle(in).trans, 0.1f);
	in = In(kNormal, 0.8f);
	in.enhancedVision = true;
	in.stealth = true;
	EXPECT_EQ(ComputeSpriteStyle(in).trans, 0.8f);
}

TEST(SpriteStyle, OnlyAnOpaqueParticleThatIsNotSmoothIsAlphaTested)
{
	EXPECT_TRUE(ComputeParticleAlphaTested(1, 1.f));
	EXPECT_FALSE(ComputeParticleAlphaTested(2, 1.f));
	EXPECT_FALSE(ComputeParticleAlphaTested(0, 0.5f));
}

TEST(SpriteStyle, BlendModesAsTheBackendDrawsThem)
{
	EXPECT_EQ(ComputeSpriteBlendMode(kOpShadow, kAlphaInvSrc, 1.f), 3);
	EXPECT_EQ(ComputeSpriteBlendMode(kOpAdd, kAlphaOne, 1.f), 2);
	EXPECT_EQ(ComputeSpriteBlendMode(kOpAdd, kAlphaInvSrc, 0.5f), 1);
	EXPECT_EQ(ComputeSpriteBlendMode(kOpAdd, kAlphaInvSrc, 1.f), 0);
	EXPECT_EQ(ComputeSpriteBlendMode(kOpSub, kAlphaInvSrc, 1.f), 0);
}
