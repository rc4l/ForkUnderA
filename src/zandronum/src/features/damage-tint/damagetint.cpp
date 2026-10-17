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
#include "features/gamecopy/players.h"   // [rc4l] the console player's body record
#include "features/gamecopy/level.h"   // [rc4l] the level as the render copy holds it
#include "doomtype.h"
#include "doomstat.h"
#include "g_level.h"
#include "actor.h"
#include "d_player.h"
#include "r_defs.h"
#include "r_data/renderstyle.h"
#include "p_local.h"
#include "p_3dfloors.h"
#include "p_terrain.h"
#include "c_dispatch.h"
#include "textures/textures.h"
#include "bitmap.h"
#include "tarray.h"
#include "c_cvars.h"

#include "features/damage-tint/computation/damagetint_compute.h"
#include "features/damage-tint/damagetint.h"
#include "features/gamecopy/tintsubject.h"

using namespace zx::damagetint;
using zx::gamecopy::TintSubject;

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
	TMap<unsigned int, TintState> g_state;
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

	// [rc4l] Advance (and occasionally prune) the per-actor ramp, keyed on the actor's render copy id, which no later actor reuses.
	float UpdateIntensity( unsigned int actor, bool active, PalEntry activeColor, PalEntry &colorOut )
	{
		TintState st = { 0.0f, zx::gamecopy::Level().time, PalEntry( 255, 255, 255 ) };
		if ( TintState *hit = g_state.CheckKey( actor ))
			st = *hit;
		if ( active )
			st.color = activeColor;
		colorOut = st.color;

		// A lastTic from the future means the level changed under us (the clock restarted and the
		// actor address was recycled) -- start this entry over instead of freezing its ramp.
		if ( st.lastTic > zx::gamecopy::Level().time )
		{
			st.intensity = 0.0f;
			st.lastTic = zx::gamecopy::Level().time;
		}

		st.intensity = RampStep( st.intensity, active, zx::gamecopy::Level().time - st.lastTic, RISE_TICS, FALL_TICS );
		st.lastTic = zx::gamecopy::Level().time;
		g_state.Insert( actor, st );

		static int lastSweep = -1;
		if ( zx::gamecopy::Level().time < lastSweep || zx::gamecopy::Level().time - lastSweep > 128 )
		{
			lastSweep = zx::gamecopy::Level().time;
			TArray<unsigned int> stale;
			TMap<unsigned int, TintState>::Iterator it( g_state );
			TMap<unsigned int, TintState>::Pair *pair;
			while ( it.NextPair( pair ))
				if ( pair->Value.lastTic > zx::gamecopy::Level().time || zx::gamecopy::Level().time - pair->Value.lastTic > 70 )
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
		if ( basePct <= 0 || gamestate != GS_LEVEL || s.id == 0 )
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

		float intensity = UpdateIntensity( s.id, active, activeColor, avgOut );
		if ( intensity <= 0.02f )
			return 0;

		float pulse = active ? PulseFactor( zx::gamecopy::Level().time & 31, PULSE_PEAK, PULSE_DECAY ) : 1.0f;
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

bool DamageTint_BeginSpriteGlow( const TintSubject &s, int blendOp, DWORD styleFlags, float vt, float vb )
{
	// Per-pixel gradient across the sprite's vertical texture span: full tint at the feet (the
	// larger V), fading to none at the coverage point. Same shader path as the weapon and
	// mugshot, so it reads identically on any clothing color; protected players glow instead.
	TintGradient g;
	if ( !DamageTint_SpriteGradient( s, blendOp, styleFlags, vt, vb, g ))
		return false;
	SetGLTint( g );
	return true;
}

bool DamageTint_Ramp( unsigned int id, float &intensity, int &lastTic )
{
	const TintState *st = g_state.CheckKey( id );
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
	// [rc4l] The console player's body as the render copy holds it, none when the copy has no such player.
	const zx::gamecopy::PlayerRecord *const me = zx::gamecopy::Player( consoleplayer );
	if ( me == NULL )
		return PalEntry( 0 );
	const zx::gamecopy::ActorRecord *const mo = me != NULL ? zx::gamecopy::Record( me->mo ) : NULL;
	const TintSubject subject = { mo != NULL ? mo->id : 0u, mo != NULL && mo->playerBody, mo != NULL && mo->tintActive, mo != NULL ? mo->tintTexture : FTextureID() };
	PalEntry avg;
	bool glow;
	// The mugshot has no render style; qualify with the neutral style so only the floor state decides.
	int pct = TintStrength( subject, STYLEOP_Add, 0, cl_damagetint_face, avg, glow );
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

CCMD( fua_tintdebug )
{
	if ( gamestate != GS_LEVEL || consoleplayer < 0 || !playeringame[consoleplayer] )
		return;
	AActor *mo = players[consoleplayer].mo;
	if ( mo == NULL || mo->Sector == NULL )
		return;
	sector_t *sec = mo->Sector;
	fixed_t fz = sec->floorplane.ZatPoint( mo->x, mo->y );
	FTextureID dbgTex;
	dbgTex.SetInvalid();
	int terrain = P_GetThingFloorType( mo );
	Printf( "sector #%d  special=%d (0x%x)  damage=%d  computed=%d  terrain=%d(dmg %d)\n",
		(int)( sec - sectors ), sec->special, sec->special, sec->damage, zx::gamecopy::FloorDamage( mo, dbgTex ),
		terrain, Terrains[terrain].DamageAmount );
	Printf( "z=%d floorz=%d planez=%d onfloor=%d waterlevel=%d protected=%d 3dfloors=%d\n",
		(int)( mo->z >> FRACBITS ), (int)( mo->floorz >> FRACBITS ), (int)( fz >> FRACBITS ),
		(int)( mo->z == fz ), mo->waterlevel, (int)( mo->player && zx::gamecopy::DamageNegated( mo->player )),
		sec->e ? (int)sec->e->XFloor.ffloors.Size() : 0 );
	float intensity;
	int lastTic;
	if ( DamageTint_Ramp( mo->rcSerial, intensity, lastTic ))
		Printf( "intensity=%.2f lastTic=%d (now %d)\n", intensity, lastTic, level.time );
	else
		Printf( "no ramp entry yet\n" );

	// Histogram of nonzero specials with a sample position for each.
	TMap<int, int> counts;
	TMap<int, int> sample;
	for ( int i = 0; i < numsectors; ++i )
	{
		int sp = sectors[i].special;
		if ( sp == 0 && sectors[i].damage == 0 ) continue;
		int key = sp;
		if ( int *c = counts.CheckKey( key )) ++*c;
		else { counts.Insert( key, 1 ); sample.Insert( key, i ); }
	}
	TMap<int, int>::Iterator it( counts );
	TMap<int, int>::Pair *p;
	while ( it.NextPair( p ))
	{
		int si = *sample.CheckKey( p->Key );
		sector_t &s = sectors[si];
		Printf( "special %d (0x%x): %d sectors, dmg-> %d, e.g. #%d near (%d,%d)\n",
			p->Key, p->Key, p->Value, DamageTint_SectorDamage( &s ), si,
			(int)( s.soundorg[0] >> FRACBITS ), (int)( s.soundorg[1] >> FRACBITS ));
	}
}

#else // NO_GL -- dedicated server: no renderer, no tint; nothing references these there.

#include "features/damage-tint/damagetint.h"
bool DamageTint_BeginSpriteGlow( const zx::gamecopy::TintSubject &, int, DWORD, float, float ) { return false; }
void DamageTint_EndSpriteGlow() {}
bool DamageTint_SpriteGradient( const zx::gamecopy::TintSubject &, int, DWORD, float, float, zx::damagetint::TintGradient & ) { return false; }
bool DamageTint_WeaponGradient( const zx::gamecopy::TintSubject &, int, DWORD, float, float, zx::damagetint::TintGradient & ) { return false; }
bool DamageTint_Armed2D( zx::damagetint::TintGradient & ) { return false; }
PalEntry DamageTint_Blend( PalEntry, int ) { return PalEntry( 255, 255, 255, 255 ); }
PalEntry DamageTint_FaceOverlay( float *cov, bool *glow ) { if ( cov ) *cov = 0.0f; if ( glow ) *glow = false; return PalEntry( 0 ); }
void DamageTint_Arm2D( PalEntry, float, bool ) {}
void DamageTint_Disarm2D() {}

#endif // NO_GL
