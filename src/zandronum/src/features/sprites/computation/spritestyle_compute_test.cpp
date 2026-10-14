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
	// [rc4l] The subtract styles subtract whatever their alpha, as GL's blend equation does.
	EXPECT_EQ(ComputeSpriteBlendMode(kOpSub, kAlphaInvSrc, 1.f), kBlendSubtract);
	EXPECT_EQ(ComputeSpriteBlendMode(kOpRevSub, kAlphaOne, 0.5f), kBlendRevSubtract);
	// [rc4l] Shadow wins over an additive destination, and a decal fading out over its life is translucent.
	EXPECT_EQ(ComputeSpriteBlendMode(kOpShadow, kAlphaOne, 0.5f), 3);
	EXPECT_EQ(ComputeSpriteBlendMode(kOpAdd, kAlphaInvSrc, 0.4f), 1);
	// [rc4l] One 8-bit step: 255/255 is opaque and 254/255 is not.
	EXPECT_EQ(ComputeSpriteBlendMode(kOpAdd, kAlphaInvSrc, 255.f / 255.f), 0);
	EXPECT_EQ(ComputeSpriteBlendMode(kOpAdd, kAlphaInvSrc, 254.f / 255.f), 1);
}

TEST(SpriteStyle, TheWeaponTakesTheFuzzAndAlphaRulesWithoutBeingMadeSolid)
{
	const SpriteStyle normal = { kOpAdd, kAlphaSrc, kAlphaInvSrc, 0 };
	const SpriteStyle translucent = { kOpAdd, kAlphaSrc, kAlphaInvSrc, 0 };
	WeaponStyleOut w = ComputeWeaponStyle(normal, false, 0.75f, 0.5f, 0, translucent);
	EXPECT_FLOAT_EQ(0.75f, w.trans);
	EXPECT_EQ(kAlphaSrc, w.style.srcAlpha);
	EXPECT_FALSE(w.noBright);
	const SpriteStyle fuzz = { kOpFuzz, kAlphaSrc, kAlphaInvSrc, 0 };
	w = ComputeWeaponStyle(fuzz, true, 1.f, 0.5f, 0, translucent);
	EXPECT_EQ(kOpShadow, w.style.blendOp);
	EXPECT_TRUE(w.noBright);
	EXPECT_FALSE(w.fuzzShader);
	EXPECT_FLOAT_EQ(1.f, w.trans);
	w = ComputeWeaponStyle(fuzz, true, 1.f, 0.5f, 2, translucent);
	EXPECT_FLOAT_EQ(0.99f, w.trans);
	EXPECT_TRUE(w.fuzzShader);
	const SpriteStyle souls = { kOpAdd, kAlphaSrc, kAlphaInvSrc, kFlagTransSouls };
	EXPECT_FLOAT_EQ(0.5f, ComputeWeaponStyle(souls, false, 1.f, 0.5f, 0, translucent).trans);
	const SpriteStyle one = { kOpAdd, kAlphaSrc, kAlphaInvSrc, kFlagAlpha1 };
	EXPECT_FLOAT_EQ(1.f, ComputeWeaponStyle(one, false, 0.25f, 0.5f, 0, translucent).trans);
	// [rc4l] A zero transsouls stays zero rather than falling back to the actor's alpha.
	EXPECT_FLOAT_EQ(0.f, ComputeWeaponStyle(souls, false, 1.f, 0.f, 0, translucent).trans);
	// [rc4l] An invisible actor's weapon at alpha 0 stays at 0.
	EXPECT_FLOAT_EQ(0.f, ComputeWeaponStyle(normal, false, 0.f, 0.5f, 0, translucent).trans);
}

// [rc4l] An imp's fireball: additive, opaque and fullbright from a sprite with no translucent texels, which r_usecolorblending blends by its own colour.
TEST(SpriteStyle, AnOpaqueFullbrightAdditiveSpriteBlendsByItsColour)
{
	const SpriteStyle add = { kOpAdd, kAlphaSrc, kAlphaOne, 0 };
	const ColorBlendIn in = { true, false, true, true, false };
	EXPECT_TRUE(ComputeColorBlending(add, 1.f, in));
	// [rc4l] Not when anything GL checks says otherwise: translucent, off, a fixed colormap, a model, not fullbright, a translucent texture.
	EXPECT_FALSE(ComputeColorBlending(add, 0.75f, in));
	ColorBlendIn off = in; off.enabled = false;
	EXPECT_FALSE(ComputeColorBlending(add, 1.f, off));
	ColorBlendIn fixed = in; fixed.fixedColormap = true;
	EXPECT_FALSE(ComputeColorBlending(add, 1.f, fixed));
	ColorBlendIn model = in; model.sprite = false;
	EXPECT_FALSE(ComputeColorBlending(add, 1.f, model));
	ColorBlendIn dim = in; dim.fullbright = false;
	EXPECT_FALSE(ComputeColorBlending(add, 1.f, dim));
	ColorBlendIn soft = in; soft.textureTransparent = true;
	EXPECT_FALSE(ComputeColorBlending(add, 1.f, soft));
	// [rc4l] Nor for a style that is not source alpha added onto one.
	EXPECT_FALSE(ComputeColorBlending(kTranslucent, 1.f, in));
	EXPECT_FALSE(ComputeColorBlending({ kOpSub, kAlphaSrc, kAlphaOne, 0 }, 1.f, in));
	EXPECT_FALSE(ComputeColorBlending({ kOpAdd, kAlphaOne, kAlphaOne, 0 }, 1.f, in));
}
