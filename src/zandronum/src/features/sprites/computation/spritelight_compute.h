// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A sprite's light as GL works it out: the floor glow, the dynamic lights that reach it, its colour, and the bands a 3D floor's light list cuts it into.

#ifndef ZX_SPRITELIGHT_COMPUTE_H
#define ZX_SPRITELIGHT_COMPUTE_H

namespace zx { namespace sprites {

// [rc4l] gl_CheckSpriteGlow: a glowing floor brightens what stands within glowHeight of it; floorDiff is the 16.16 height above the floor.
int ComputeSpriteGlow(int lightLevel, int floorDiff, int glowHeight);

// [rc4l] One dynamic light's share of gl_SetDynSpriteLight, added to out; rgb already carry r_lights_intensity, and bright is their length for a subtractive light.
void ComputeSpriteLightTerm(float dist, float radius, float r, float g, float b, bool subtractive, float bright, float out[3]);

// [rc4l] FColormap::Decolorize, for glset.nocoloredspritelighting: grey, and lighter than the mean.
void ComputeDecolorize(int &r, int &g, int &b);

// [rc4l] A fullbright sprite's light colour: white, or three quarters of the way there below an underwater surface.
void ComputeFullbrightColor(bool underwater, int &r, int &g, int &b);

// [rc4l] One band of a sprite split by a 3D floor's light list: its top and bottom, v at each, and the list entry that lights it.
struct SpriteBand { float z1, z2, vt, vb; int entry; };

// [rc4l] GLSprite::SplitSprite: `bottoms` holds each entry's lower edge at the actor; returns the bands drawn, at most n.
int ComputeSpriteBands(float z1, float z2, float vt, float vb, const float *bottoms, int n, SpriteBand *out);

// [rc4l] GLSprite::SetSpriteColor: the light list entry whose band holds `centre`, or -1 to keep the sector's light.
int ComputeSpriteColorEntry(float z2, float centre, const float *bottoms, int n);

}} // namespace zx::sprites

#endif // ZX_SPRITELIGHT_COMPUTE_H
