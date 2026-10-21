// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/spritestyle_compute.h"

#include <float.h>
#include <math.h>

namespace zx { namespace sprites {

namespace {

// [rc4l] A fuzz style becomes the translucent one at 0.99 under a fuzz shader, else a shadow; true when it was one.
bool ResolveFuzz(SpriteStyle &style, float &trans, int fuzzType, const SpriteStyle &translucent)
{
	if (style.blendOp != kOpFuzz) return false;
	if (fuzzType != 0)
	{
		style = translucent;
		trans = 0.99f;
	}
	else style.blendOp = kOpShadow;
	return true;
}

// [rc4l] Transsouls and alpha1 set the alpha whatever it was.
void ApplyStyleAlpha(const SpriteStyle &style, float transSouls, float &trans)
{
	if (style.flags & kFlagTransSouls) trans = transSouls;
	else if (style.flags & kFlagAlpha1) trans = 1.f;
}

} // namespace

SpriteStyleOut ComputeSpriteStyle(const SpriteStyleIn &in)
{
	SpriteStyleOut out;
	out.style = in.style;
	out.trans = in.alpha;
	out.hw = kHwNormal;
	out.overrideShader = -1;

	if (ResolveFuzz(out.style, out.trans, in.fuzzType, in.translucent) && in.fuzzType != 0)
	{
		out.overrideShader = in.fuzzType + 4;
		out.hw |= kHwNoAlphaTest;
	}
	ApplyStyleAlpha(out.style, in.transSouls, out.trans);

	if (out.trans >= 1.f - FLT_EPSILON && out.style.blendOp != kOpShadow &&
	    ((out.style.srcAlpha == kAlphaOne && out.style.destAlpha == kAlphaZero) ||
	     (out.style.srcAlpha == kAlphaSrc && out.style.destAlpha == kAlphaInvSrc)))
	{
		// [rc4l] A non-translucent sprite, STYLE_Normal or its equivalent.
		out.trans = 1.f;
		if (!in.spriteBlend || in.model)
		{
			out.style.srcAlpha = kAlphaOne;
			out.style.destAlpha = kAlphaZero;
			out.hw = kHwSolid;
		}
		else
		{
			out.style.srcAlpha = kAlphaSrc;
			out.style.destAlpha = kAlphaInvSrc;
		}
	}
	if (in.textureTransparent || (out.style.flags & kFlagRedIsAlpha))
	{
		if (out.hw == kHwSolid)
		{
			out.style.srcAlpha = kAlphaSrc;
			out.style.destAlpha = kAlphaInvSrc;
		}
		out.hw = kHwNoAlphaTest;
	}
	if (in.enhancedVision)
	{
		if (out.style.blendOp == kOpShadow)
		{
			// [rc4l] Enhanced vision makes a fuzz shadow plainly visible, keeping only its flags.
			out.trans = 0.5f;
			const int flags = out.style.flags;
			out.style = in.translucent;
			out.style.flags = flags;
		}
		else if (in.stealth && out.trans < 0.5f) out.trans = 0.5f;
	}
	return out;
}

WeaponStyleOut ComputeWeaponStyle(const SpriteStyle &style, bool fuzzStyle, float alpha, float transSouls, int fuzzType, const SpriteStyle &translucent)
{
	WeaponStyleOut out;
	out.style = style;
	out.trans = 0.f;
	out.noBright = fuzzStyle;
	out.fuzzShader = fuzzStyle && fuzzType != 0;
	if (fuzzStyle) ResolveFuzz(out.style, out.trans, fuzzType, translucent);
	ApplyStyleAlpha(out.style, transSouls, out.trans);
	if (out.trans == 0.f && !(out.style.flags & (kFlagTransSouls | kFlagAlpha1))) out.trans = alpha;
	return out;
}

int ComputeSpriteBlendMode(int blendOp, int destAlpha, float trans)
{
	if (blendOp >= kOpFuzz) return kBlendFuzz;
	if (blendOp == kOpRevSub) return kBlendRevSubtract;
	if (blendOp == kOpSub) return kBlendSubtract;
	if (blendOp == kOpAdd && destAlpha == kAlphaOne) return kBlendAdditive;
	return trans < 1.f - 1.f / 256.f ? kBlendTranslucent : kBlendOpaque;
}

int ComputeModelBlendOp(int drawOp, int actorOp, int fuzzOp)
{
	return drawOp == fuzzOp ? actorOp : drawOp;
}

SpriteDrawLook ComputePlainDrawLook(float alpha)
{
	const SpriteDrawLook look = { false, false, false, alpha };
	return look;
}

int ComputeModelCull(bool normalStyle, bool hud)
{
	if (normalStyle) return kCullNone;
	return hud ? kCullFrontCounterClockwise : kCullFrontClockwise;
}

SpriteDrawLook ComputeSpriteDrawLook(const SpriteStyle &style, float trans, bool blackFog, int fadeAlpha, float dist, bool model)
{
	SpriteDrawLook out;
	out.shadow = style.blendOp == kOpShadow;
	out.fogLayer = !model && !blackFog && (style.blendOp == kOpSub || style.blendOp == kOpRevSub);
	out.additiveFog = out.shadow || (style.blendOp == kOpAdd && style.destAlpha == kAlphaOne) || (style.flags & kFlagFadeToBlack) != 0;
	// [rc4l] The factor was found by trial in GL and is scale dependent, so it is GL's to the digit.
	out.alpha = !out.shadow ? trans : blackFog ? 0.44f : 0.44f * (0.05f + expf(-fadeAlpha * dist / 62500.f));
	return out;
}

bool ComputeColorBlending(const SpriteStyle &style, float trans, const ColorBlendIn &in)
{
	const bool allowed = trans > 1.f - FLT_EPSILON && in.enabled && !in.fixedColormap && in.sprite && in.fullbright && !in.textureTransparent;
	return allowed && style.blendOp == kOpAdd && style.srcAlpha == kAlphaSrc && style.destAlpha == kAlphaOne;
}

bool ComputeParticleAlphaTested(int particlesStyle, float trans)
{
	return particlesStyle != 2 && trans >= 1.0f - FLT_EPSILON;
}

}} // namespace zx::sprites
