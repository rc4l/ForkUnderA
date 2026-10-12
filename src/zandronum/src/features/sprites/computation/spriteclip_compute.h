// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GLSprite::PerformSpriteClipAdjustment: how far GL lifts a sprite out of the floor, or drops it from the ceiling, under gl_spriteclip.

#ifndef ZX_SPRITECLIP_COMPUTE_H
#define ZX_SPRITECLIP_COMPUTE_H

namespace zx { namespace sprites {

// [rc4l] What the clip asks of the actor, and the floor and ceiling it stands between.
struct SpriteClipIn
{
	bool  living;          // [rc4l] a player, monster or inventory item that is not a corpse (an ice corpse counts)
	bool  monster, noGravity, corpse;
	int   spriteClip;      // [rc4l] gl_spriteclip
	float clipFactor;      // [rc4l] gl_sclipfactor
	float clipThreshold;   // [rc4l] gl_sclipthreshold
	float bottom, top;     // [rc4l] the floor and ceiling heights GL measures against
	float spriteHeight;
};

// [rc4l] Whether the clip runs at all, so the caller measures the floor and ceiling only when it does.
bool ComputeSpriteClips(bool living, int spriteClip);

// [rc4l] Shift z1 and z2 as GL does; call only when ComputeSpriteClips says so.
void ComputeSpriteClip(const SpriteClipIn &in, float &z1, float &z2);

}} // namespace zx::sprites

#endif // ZX_SPRITECLIP_COMPUTE_H
