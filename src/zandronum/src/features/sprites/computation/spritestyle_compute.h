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
	int   fuzzType;            // [rc4l] r_fuzztype
	bool  spriteBlend;         // [rc4l] r_sprite_blend
	bool  model;               // [rc4l] drawn as a model
	bool  textureTransparent;  // [rc4l] the frame's texture has translucent texels
	bool  enhancedVision;      // [rc4l] r_enhanced_nightvision with a fixed colormap that reveals this
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
	bool fuzzShader; // [rc4l] it was a fuzz style and r_fuzztype draws it with a fuzz shader, as GL's OverrideShader
};

// [rc4l] DrawPlayerSprites' style and alpha: the sprites' fuzz, transsouls and alpha1 rules without their solid-sprite normalisation; `fuzzStyle` is whether the style was a fuzz one before FRenderStyle::CheckFuzz settled `style`.
WeaponStyleOut ComputeWeaponStyle(const SpriteStyle &style, bool fuzzStyle, float alpha, float transSouls, int fuzzType, const SpriteStyle &translucent);

// [rc4l] The backend's blends: opaque or alpha-tested, translucent, additive, fuzz, and GL_FUNC_REVERSE_SUBTRACT's and GL_FUNC_SUBTRACT's, the screen less the sprite and the sprite less the screen.
enum { kBlendOpaque = 0, kBlendTranslucent = 1, kBlendAdditive = 2, kBlendFuzz = 3, kBlendRevSubtract = 4, kBlendSubtract = 5 };

// [rc4l] The backend's blend (a kBlend* value) for a resolved style, for sprites, the weapon and decals alike.
int ComputeSpriteBlendMode(int blendOp, int destAlpha, float trans);

// [rc4l] What GLSprite::Draw's allowcolorblending reads of an actor besides its style.
struct ColorBlendIn
{
	bool enabled;              // [rc4l] r_usecolorblending
	bool fixedColormap;        // [rc4l] a fixed colormap is on
	bool sprite;               // [rc4l] drawn from a sprite texture, which a model is not
	bool fullbright;
	bool textureTransparent;   // [rc4l] the frame's texture has translucent texels
};

// [rc4l] gl_GetRenderStyle's colour blending: an opaque additive style (source alpha onto one) that blends by the source's own colour instead, colour squared onto the screen, when ColorBlendIn allows it.
bool ComputeColorBlending(const SpriteStyle &style, float trans, const ColorBlendIn &in);

// [rc4l] ProcessParticle's alpha test: a fully opaque particle that is not a smooth one (r_particles_style 2).
bool ComputeParticleAlphaTested(int particlesStyle, float trans);

}} // namespace zx::sprites

#endif // ZX_SPRITESTYLE_COMPUTE_H
