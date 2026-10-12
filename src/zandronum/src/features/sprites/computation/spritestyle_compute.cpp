// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/spritestyle_compute.h"

#include <float.h>

namespace zx { namespace sprites {

SpriteStyleOut ComputeSpriteStyle(const SpriteStyleIn &in)
{
	SpriteStyleOut out;
	out.style = in.style;
	out.trans = in.alpha;
	out.hw = kHwNormal;
	out.overrideShader = -1;

	if (out.style.blendOp == kOpFuzz)
	{
		if (in.fuzzType != 0)
		{
			out.style = in.translucent;
			out.overrideShader = in.fuzzType + 4;
			out.trans = 0.99f;
			out.hw |= kHwNoAlphaTest;
		}
		else out.style.blendOp = kOpShadow;
	}

	if (out.style.flags & kFlagTransSouls) out.trans = in.transSouls;
	else if (out.style.flags & kFlagAlpha1) out.trans = 1.f;

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

int ComputeSpriteBlendMode(int blendOp, int destAlpha, float trans)
{
	if (blendOp == kOpShadow) return 3;
	if (blendOp == kOpAdd && destAlpha == kAlphaOne) return 2;
	return trans < 1.f - 1.f / 256.f ? 1 : 0;
}

}} // namespace zx::sprites
