// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] features/damage-tint's render half, the ramps, floor colours and hooks, with whether a floor hurts an actor read between tics by tintsubject.cpp and the arithmetic in computation/damagetint_compute.
//
// Render-side only by design: nothing here writes sector_t, AActor, or anything a savegame, demo,
// netcode, or ACS script can observe. Sector_SetColor etc. are irrelevant to it -- the tint reads
// the floor TEXTURE, not the sector's light -- and mods that restyle a sprite (stencil/fuzz/...)
// win automatically via the blend-op gate.

#include "gl/system/gl_system.h"
#include "doomtype.h"
#include "doomstat.h"
#include "g_level.h"
#include "actor.h"
#include "d_player.h"
#include "r_defs.h"
#include "r_data/renderstyle.h"
#include "p_local.h"
#include "p_3dfloors.h"
#include "c_dispatch.h"
#include "textures/textures.h"
#include "bitmap.h"
#include "tarray.h"
#include "c_cvars.h"

#include "features/damage-tint/computation/damagetint_compute.h"
#include "features/damage-tint/damagetint.h"
#include "features/damage-tint/tintsubject.h"

using namespace zx::damagetint;

#ifndef NO_GL

#include "gl/renderer/gl_renderstate.h"

// Strength (color intensity) and coverage (how far up the sprite the gradient reaches, as a percent
// of the body / weapon-quad height). Either at 0 disables that surface. Face is strength-only.
CVAR( Int, cl_damagetint, 100, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_coverage, 50, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_weapon, 100, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_weapon_coverage, 50, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_face, 65, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_face_coverage, 50, CVAR_ARCHIVE )

// [rc4l] averageColor reads RGBA and FBitmap stores BGRA, so its red and blue come back swapped and the callers swap them back.
#include "features/materials/textureinfo.h"

namespace
{
	const float RISE_TICS  = 8.0f;   // ~quarter second to full tint while standing in it
	const float FALL_TICS  = 16.0f;  // ~half second linger after stepping out
	const float PULSE_PEAK = 0.3f;   // throb amplitude on each 32-tic damage application
	const int   PULSE_DECAY = 6;     // tics for the throb to settle

	struct TintState
	{
		float    intensity;
		int      lastTic;
		PalEntry color; // latched while active, so a fade-out keeps the pool's color
	};
	TMap<const void *, TintState> g_state;
	TMap<int, PalEntry> g_avgCache; // floor texture id -> average color

	PalEntry FloorAverageColor( FTextureID id )
	{
		if ( !id.isValid() )
			return PalEntry( 255, 255, 255 );
		if ( PalEntry *hit = g_avgCache.CheckKey( id.GetIndex() ))
			return *hit;

		PalEntry avg( 255, 255, 255 );
		FTexture *tex = TexMan[id];
		if ( tex != NULL )
		{
			int w = tex->GetWidth(), h = tex->GetHeight();
			FBitmap bmp;
			if ( w > 0 && h > 0 && bmp.Create( w, h ))
			{
				tex->CopyTrueColorPixels( &bmp, 0, 0 );
				PalEntry sw = averageColor( (const DWORD *)bmp.GetPixels(), w * h, FRACUNIT );
				avg = PalEntry( 255, sw.b, sw.g, sw.r ); // undo the BGRA/RGBA swap
			}
		}
		g_avgCache.Insert( id.GetIndex(), avg );
		return avg;
	}

	// Advance (and occasionally prune) the per-actor ramp. Keyed on the actor pointer; an address
	// reused by a new actor can inherit at most one frame of stale fade-out -- cosmetic only.
	float UpdateIntensity( const void *actor, bool active, PalEntry activeColor, PalEntry &colorOut )
	{
		TintState st = { 0.0f, level.time, PalEntry( 255, 255, 255 ) };
		if ( TintState *hit = g_state.CheckKey( actor ))
			st = *hit;
		if ( active )
			st.color = activeColor;
		colorOut = st.color;

		// A lastTic from the future means the level changed under us (the clock restarted and the
		// actor address was recycled) -- start this entry over instead of freezing its ramp.
		if ( st.lastTic > level.time )
		{
			st.intensity = 0.0f;
			st.lastTic = level.time;
		}

		st.intensity = RampStep( st.intensity, active, level.time - st.lastTic, RISE_TICS, FALL_TICS );
		st.lastTic = level.time;
		g_state.Insert( actor, st );

		static int lastSweep = -1;
		if ( level.time < lastSweep || level.time - lastSweep > 128 )
		{
			lastSweep = level.time;
			TArray<const void *> stale;
			TMap<const void *, TintState>::Iterator it( g_state );
			TMap<const void *, TintState>::Pair *pair;
			while ( it.NextPair( pair ))
				if ( pair->Value.lastTic > level.time || level.time - pair->Value.lastTic > 70 )
					stale.Push( pair->Key );
			for ( unsigned int i = 0; i < stale.Size(); ++i )
				g_state.Remove( stale[i] );
		}
		return st.intensity;
	}

	// Shared qualification + strength for the hooks. Returns the effective percent (0 = no tint),
	// the tint color, and whether it should render as the protected GLOW (radsuit/invuln/god on a
	// damaging floor) instead of the taking-damage stain.
	int TintStrength( const TintSubject &s, int blendOp, DWORD styleFlags, int basePct, PalEntry &avgOut, bool &glowOut )
	{
		glowOut = false;
		if ( basePct <= 0 || gamestate != GS_LEVEL || s.who == NULL )
			return 0;
		if ( !s.playerBody )
			return 0; // players only: floor damage is a player mechanic
		if ( blendOp != STYLEOP_Add || ( styleFlags & STYLEF_ColorIsFixed ))
			return 0; // stencil/shaded/fuzz/subtractive styles own their look

		// [rc4l] The subject is active only when tintsubject.cpp found its floor hurting it unprotected, and nothing sets glowOut today.
		const bool active = s.active;

		PalEntry activeColor( 255, 255, 255 );
		if ( active )
			activeColor = FloorAverageColor( s.texture );

		float intensity = UpdateIntensity( s.who, active, activeColor, avgOut );
		if ( intensity <= 0.02f )
			return 0;

		float pulse = active ? PulseFactor( level.time & 31, PULSE_PEAK, PULSE_DECAY ) : 1.0f;
		return EffectivePct( basePct, intensity, pulse );
	}
} // namespace

namespace
{
	// [rc4l] An actor's gradient at its hook's strength and coverage cvars: full at the larger V, as the sprite, weapon and face all take it.
	bool Gradient( const TintSubject &s, int blendOp, DWORD styleFlags, int basePct, int coveragePct, float vt, float vb, TintGradient &out )
	{
		PalEntry avg;
		bool glow;
		const int pct = TintStrength( s, blendOp, styleFlags, basePct, avg, glow );
		const int cov = clamp<int>( coveragePct, 0, 100 );
		if ( pct <= 0 || cov <= 0 )
			return false;
		out = ComputeTintGradient( avg.r / 255.0f, avg.g / 255.0f, avg.b / 255.0f, pct / 100.0f, vt, vb, cov / 100.0f, glow );
		return true;
	}

	void SetGLTint( const TintGradient &g )
	{
		gl_RenderState.SetDamageTint( g.r, g.g, g.b, g.strength, g.top, g.bottom, g.glow );
	}

	// [rc4l] What DamageTint_Arm2D armed, held for a renderer that records the 2D draw rather than drawing it at once.
	TintGradient g_armed2D;
	bool g_armed2DOn = false;
} // namespace

bool DamageTint_SpriteGradient( const TintSubject &s, int blendOp, DWORD styleFlags, float vt, float vb, TintGradient &out )
{
	return Gradient( s, blendOp, styleFlags, cl_damagetint, cl_damagetint_coverage, vt, vb, out );
}

bool DamageTint_WeaponGradient( const TintSubject &s, int blendOp, DWORD styleFlags, float vt, float vb, TintGradient &out )
{
	return Gradient( s, blendOp, styleFlags, cl_damagetint_weapon, cl_damagetint_weapon_coverage, vt, vb, out );
}

bool DamageTint_BeginSpriteGlow( AActor *actor, int blendOp, DWORD styleFlags, float vt, float vb )
{
	// Per-pixel gradient across the sprite's vertical texture span: full tint at the feet (the
	// larger V), fading to none at the coverage point. Same shader path as the weapon and
	// mugshot, so it reads identically on any clothing color; protected players glow instead.
	TintGradient g;
	if ( !DamageTint_SpriteGradient( TintSubjectOf( actor ), blendOp, styleFlags, vt, vb, g ))
		return false;
	SetGLTint( g );
	return true;
}

bool DamageTint_Ramp( const void *who, float &intensity, int &lastTic )
{
	const TintState *st = g_state.CheckKey( who );
	if ( st == NULL )
		return false;
	intensity = st->intensity;
	lastTic = st->lastTic;
	return true;
}

void DamageTint_EndSpriteGlow()
{
	gl_RenderState.ClearDamageTint();
}

PalEntry DamageTint_Blend( PalEntry avg, int pct )
{
	return PalEntry( 255, (BYTE)TintChannel( avg.r, pct ), (BYTE)TintChannel( avg.g, pct ), (BYTE)TintChannel( avg.b, pct ));
}

void DamageTint_Arm2D( PalEntry overlay, float coverageFrac, bool glow )
{
	if ( overlay.a == 0 || coverageFrac <= 0.0f )
		return;
	// Texture-V runs 0 (top) to 1 (bottom) on a 2D graphic: gradient bottom = 1, top = 1 - coverage.
	g_armed2D = ComputeTintGradient( overlay.r / 255.0f, overlay.g / 255.0f, overlay.b / 255.0f, overlay.a / 255.0f,
		0.0f, 1.0f, coverageFrac, glow );
	g_armed2DOn = true;
	SetGLTint( g_armed2D );
}

void DamageTint_Disarm2D()
{
	g_armed2DOn = false;
	gl_RenderState.ClearDamageTint();
}

bool DamageTint_Armed2D( TintGradient &out )
{
	if ( !g_armed2DOn )
		return false;
	out = g_armed2D;
	return true;
}

PalEntry DamageTint_FaceOverlay( float *coverageFracOut, bool *glowOut )
{
	if ( coverageFracOut != NULL )
		*coverageFracOut = 0.0f;
	if ( glowOut != NULL )
		*glowOut = false;
	if ( consoleplayer < 0 || consoleplayer >= MAXPLAYERS || !playeringame[consoleplayer] )
		return PalEntry( 0 );
	AActor *mo = players[consoleplayer].mo;
	PalEntry avg;
	bool glow;
	// The mugshot has no render style; qualify with the neutral style so only the floor state decides.
	int pct = TintStrength( TintSubjectOf( mo ), STYLEOP_Add, 0, cl_damagetint_face, avg, glow );
	if ( pct <= 0 )
		return PalEntry( 0 );
	int cov = clamp<int>( cl_damagetint_face_coverage, 0, 100 );
	if ( cov <= 0 )
		return PalEntry( 0 );
	if ( coverageFracOut != NULL )
		*coverageFracOut = cov / 100.0f;
	if ( glowOut != NULL )
		*glowOut = glow;
	return PalEntry( (BYTE)( pct * 255 / 100 ), avg.r, avg.g, avg.b );
}

#else // NO_GL -- dedicated server: no renderer, no tint; nothing references these there.

#include "features/damage-tint/damagetint.h"
bool DamageTint_BeginSpriteGlow( AActor *, int, DWORD, float, float ) { return false; }
void DamageTint_EndSpriteGlow() {}
bool DamageTint_SpriteGradient( const zx::damagetint::TintSubject &, int, DWORD, float, float, zx::damagetint::TintGradient & ) { return false; }
bool DamageTint_WeaponGradient( const zx::damagetint::TintSubject &, int, DWORD, float, float, zx::damagetint::TintGradient & ) { return false; }
bool DamageTint_Armed2D( zx::damagetint::TintGradient & ) { return false; }
PalEntry DamageTint_Blend( PalEntry, int ) { return PalEntry( 255, 255, 255, 255 ); }
PalEntry DamageTint_FaceOverlay( float *cov, bool *glow ) { if ( cov ) *cov = 0.0f; if ( glow ) *glow = false; return PalEntry( 0 ); }
void DamageTint_Arm2D( PalEntry, float, bool ) {}
void DamageTint_Disarm2D() {}

#endif // NO_GL
