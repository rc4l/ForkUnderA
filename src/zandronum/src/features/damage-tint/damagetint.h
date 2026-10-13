// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] features/damage-tint -- players standing on damaging floors pick up the floor's color:
// an emissive gradient crawling up from the feet (via the renderer's existing glow-plane shader
// path) on body sprites, and a faint flat tint on the first-person weapon. Client-side visual
// only: derived from replicated state, no sim contact, no net traffic. Render styles that own
// their color (stencil, fuzz, subtractive) are never touched.

#ifndef ZX_DAMAGETINT_H
#define ZX_DAMAGETINT_H

#include "doomtype.h"

class AActor;

// Body sprites: if the actor qualifies (player, normal blend style, on a damaging floor or still
// fading out), arm the shader's per-pixel multiplicative gradient across the sprite's vertical
// texture span (vt..vb -- feet at the patch bottom) and return true. The caller MUST call
// DamageTint_EndSpriteGlow() after its draw when this returns true. (An additive glow was tried
// first; adding to the LIGHT gets multiplied by the texel, so a red tint on green armor vanished.)
bool DamageTint_BeginSpriteGlow( AActor *actor, int blendOp, DWORD styleFlags, float vt, float vb );
void DamageTint_EndSpriteGlow();

namespace zx { namespace damagetint { struct TintGradient; } }

// [rc4l] A body sprite's gradient over its texture span vt..vb when the actor qualifies, for whichever renderer draws it; false for none.
bool DamageTint_SpriteGradient( AActor *actor, int blendOp, DWORD styleFlags, float vt, float vb, zx::damagetint::TintGradient &out );

// [rc4l] The first-person weapon's gradient over its frame's vt..vb, judged by the player's own style; false for none.
bool DamageTint_WeaponGradient( AActor *playermo, int blendOp, DWORD styleFlags, float vt, float vb, zx::damagetint::TintGradient &out );

// [rc4l] The gradient DamageTint_Arm2D armed for the next 2D draw, for a renderer that records 2D; false while none is.
bool DamageTint_Armed2D( zx::damagetint::TintGradient &out );

// White blended toward `avg` by pct -- the multiplicative tint color for a slice.
PalEntry DamageTint_Blend( PalEntry avg, int pct );

// 2D path (the status-bar mugshot): arm the shader's per-pixel gradient for the next draw and
// clear it right after. No-ops when overlay.a == 0 (and on NO_GL builds).
void DamageTint_Arm2D( PalEntry overlay, float coverageFrac, bool glow = false );
void DamageTint_Disarm2D();

// Status-bar mugshot: the tint as an ARGB overlay (alpha 0 = inactive) plus the coverage fraction
// (0..1 from the chin up) the caller should band-clip to.
PalEntry DamageTint_FaceOverlay( float *coverageFracOut = 0, bool *glowOut = 0 );

// How much a sector's special/damage config hurts per cycle (0 = harmless). The complete mirror of
// P_PlayerInSpecialSector's tables; exported for the bridge's world.sectors query.
struct sector_t;
int DamageTint_SectorDamage( const sector_t *sec );

#endif
