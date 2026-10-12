// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A sprite's render style as GLSprite::Process settles it, and the blend the backend draws it with.

#ifndef ZX_SPRITESTYLE_COMPUTE_H
#define ZX_SPRITESTYLE_COMPUTE_H

namespace zx { namespace sprites {

// [rc4l] The engine's ERenderOp, ERenderAlpha and ERenderFlags values this reads (spritebuild.cpp asserts they match).
enum { kOpNone = 0, kOpAdd = 1, kOpSub = 2, kOpRevSub = 3, kOpFuzz = 4, kOpShadow = 8 };
enum { kAlphaZero = 0, kAlphaOne = 1, kAlphaSrc = 2, kAlphaInvSrc = 3 };
enum { kFlagTransSouls = 1, kFlagAlpha1 = 2, kFlagRedIsAlpha = 4, kFlagFadeToBlack = 64 };
// [rc4l] GLSprite's HWRenderStyle.
enum { kHwNormal = 0, kHwSolid = 1, kHwNoAlphaTest = 2 };

struct SpriteStyle
{
	int blendOp, srcAlpha, destAlpha, flags;
};

// [rc4l] What decides the style besides the actor's own: the cvars GL reads and the frame it draws.
struct SpriteStyleIn
{
	SpriteStyle style;         // [rc4l] the actor's, after FRenderStyle::CheckFuzz for a fuzz style
	float alpha;               // [rc4l] the actor's alpha, 0..1
	float transSouls;          // [rc4l] transsouls
	int   fuzzType;            // [rc4l] gl_fuzztype
	bool  spriteBlend;         // [rc4l] gl_sprite_blend
	bool  model;               // [rc4l] drawn as a model
	bool  textureTransparent;  // [rc4l] the frame's texture has translucent texels
	bool  enhancedVision;      // [rc4l] gl_enhanced_nightvision with a fixed colormap that reveals this
	bool  stealth;             // [rc4l] MF_STEALTH
	SpriteStyle translucent;   // [rc4l] LegacyRenderStyles[STYLE_Translucent]
};

struct SpriteStyleOut
{
	SpriteStyle style;
	float trans;
	int   hw;                  // [rc4l] kHw*
	int   overrideShader;      // [rc4l] -1, or the fuzz shader GL swaps in
};

// [rc4l] The style, alpha and alpha-test choice GLSprite::Process makes; trans 0 means GL draws nothing.
SpriteStyleOut ComputeSpriteStyle(const SpriteStyleIn &in);

struct WeaponStyleOut
{
	SpriteStyle style;
	float trans;
	bool noBright;   // [rc4l] a fuzz style is never drawn fullbright
};

// [rc4l] DrawPlayerSprites' style and alpha: the sprites' fuzz, transsouls and alpha1 rules without their solid-sprite normalisation; `fuzzStyle` is whether the style was a fuzz one before FRenderStyle::CheckFuzz settled `style`.
WeaponStyleOut ComputeWeaponStyle(const SpriteStyle &style, bool fuzzStyle, float alpha, float transSouls, int fuzzType, const SpriteStyle &translucent);

// [rc4l] The backend's blend for a resolved style: 0 opaque or alpha-tested, 1 translucent, 2 additive, 3 fuzz.
int ComputeSpriteBlendMode(int blendOp, int destAlpha, float trans);

// [rc4l] ProcessParticle's alpha test: a fully opaque particle that is not a smooth one (gl_particles_style 2).
bool ComputeParticleAlphaTested(int particlesStyle, float trans);

}} // namespace zx::sprites

#endif // ZX_SPRITESTYLE_COMPUTE_H
