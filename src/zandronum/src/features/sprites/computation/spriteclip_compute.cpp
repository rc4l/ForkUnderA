// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/spriteclip_compute.h"

#include <math.h>

namespace zx { namespace sprites {

static float ClampRatio(float v) { return v < 0.5f ? 0.5f : (v > 1.0f ? 1.0f : v); }

bool ComputeSpriteClips(bool living, int spriteClip)
{
	return living || spriteClip == 3 || spriteClip > 1;
}

void ComputeSpriteClip(const SpriteClipIn &in, float &z1, float &z2)
{
	// [rc4l] GL's || stops at the first true term, so the smarter clip is for what is not alive.
	const bool smarterclip = !in.living && in.spriteClip == 3;
	float diffb = (z2 + 1) - in.bottom;
	float difft = (z1 - 1) - in.top;
	if (diffb >= 0) diffb = 0;
	if (smarterclip)
	{
		// [rc4l] A corpse, or a sprite taller than its sink, is lifted a little less.
		if (in.corpse || in.spriteHeight > fabsf(diffb))
		{
			const float ratio = ClampRatio(fabsf(diffb) * in.clipFactor / (in.spriteHeight + 1));
			diffb *= ratio;
		}
		if (!diffb)
		{
			if (difft <= 0) difft = 0;
			if (difft >= in.clipThreshold)
			{
				if (!in.monster || in.noGravity || in.corpse || difft > in.clipThreshold) difft = 0;
			}
			if (in.spriteHeight > fabsf(difft))
			{
				const float ratio = ClampRatio(fabsf(difft) * in.clipFactor / (in.spriteHeight + 1));
				difft *= ratio;
			}
			z2 -= difft;
			z1 -= difft;
		}
	}
	// [rc4l] A displacement this large cannot be right, except for a living monster standing on the floor.
	if (diffb <= (0 - in.clipThreshold))
	{
		if (!in.monster || in.noGravity || in.corpse || diffb < (-1.8 * in.clipThreshold)) diffb = 0;
	}
	z2 -= diffb;
	z1 -= diffb;
}

}} // namespace zx::sprites
