// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] features/damage-tint glue: eligibility (players on damaging floors, minus radsuits),
// per-actor intensity ramps, the per-texture floor-color cache, and the two render hooks. All the
// arithmetic lives in computation/damagetint_compute (unit-tested); this file is engine plumbing.
//
// Render-side only by design: nothing here writes sector_t, AActor, or anything a savegame, demo,
// netcode, or ACS script can observe. Sector_SetColor etc. are irrelevant to it -- the tint reads
// the floor TEXTURE, not the sector's light -- and mods that restyle a sprite (stencil/fuzz/...)
// win automatically via the blend-op gate.

#ifndef NO_GL

#include "doomtype.h"
#include "doomstat.h"
#include "g_level.h"
#include "actor.h"
#include "d_player.h"
#include "r_defs.h"
#include "r_data/renderstyle.h"
#include "p_lnspec.h"
#include "a_artifacts.h"
#include "textures/textures.h"
#include "bitmap.h"
#include "tarray.h"
#include "c_cvars.h"
#include "gl/system/gl_system.h"
#include "gl/renderer/gl_renderstate.h"

#include "features/damage-tint/computation/damagetint_compute.h"
#include "features/damage-tint/damagetint.h"

using namespace zx::damagetint;

// Strength (color intensity) and coverage (how far up the sprite the gradient reaches, as a percent
// of the body / weapon-quad height). Either at 0 disables that surface. Face is strength-only.
CVAR( Int, cl_damagetint, 35, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_coverage, 50, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_weapon, 25, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_weapon_coverage, 40, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_face, 30, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_face_coverage, 60, CVAR_ARCHIVE )

// Defined in gl/textures/gl_texture.cpp. NOTE: it reads GL-order RGBA; FBitmap stores BGRA, so
// r/b come back swapped -- callers swap them back (learned the hard way in the sky-tint trial).
PalEntry averageColor( const DWORD *data, int size, fixed_t maxout_factor );

namespace
{
	const float RISE_TICS  = 8.0f;   // ~quarter second to full tint while standing in it
	const float FALL_TICS  = 16.0f;  // ~half second linger after stepping out
	const float PULSE_PEAK = 0.3f;   // throb amplitude on each 32-tic damage application
	const int   PULSE_DECAY = 6;     // tics for the throb to settle

	struct TintState
	{
		float intensity;
		int   lastTic;
	};
	TMap<AActor *, TintState> g_state;
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

	// How much a floor hurts. Mirrors the damaging cases of P_PlayerInSpecialSector: the generic
	// damage field, the classic named specials, and Boom's generalized damage bits.
	int FloorDamageAmount( const sector_t *sec )
	{
		if ( sec == NULL )
			return 0;
		if ( sec->damage > 0 )
			return sec->damage;

		switch ( sec->special & 0xff )
		{
		case dDamage_Nukage:
		case dDamage_LavaWimpy:
		case dScroll_EastLavaDamage:
			return 5;
		case dDamage_Hellslime:
		case dDamage_LavaHefty:
			return 10;
		case hDamage_Sludge:
			return 4;
		case dDamage_SuperHellslime:
		case dDamage_End:
		case sDamage_SuperHellslime:
			return 20;
		case sDamage_Hellslime:
			return 2;
		case Damage_InstantDeath:
			return 999;
		}

		// Boom generalized sectors: bits 5-6 select 5/10/20 per cycle.
		switch ( sec->special & 0x0300 )
		{
		case 0x0100: return 5;
		case 0x0200: return 10;
		case 0x0300: return 20;
		}
		return 0;
	}

	bool SuitProtected( player_t *player )
	{
		for ( AInventory *item = player->mo->Inventory; item != NULL; item = item->Inventory )
			if ( item->IsKindOf( RUNTIME_CLASS( APowerIronFeet )))
				return true;
		return false;
	}

	// Advance (and occasionally prune) the per-actor ramp. Keyed on the actor pointer; an address
	// reused by a new actor can inherit at most one frame of stale fade-out -- cosmetic only.
	float UpdateIntensity( AActor *actor, bool active )
	{
		TintState st = { 0.0f, level.time };
		if ( TintState *hit = g_state.CheckKey( actor ))
			st = *hit;

		st.intensity = RampStep( st.intensity, active, level.time - st.lastTic, RISE_TICS, FALL_TICS );
		st.lastTic = level.time;
		g_state.Insert( actor, st );

		static int lastSweep = -1;
		if ( level.time < lastSweep || level.time - lastSweep > 128 )
		{
			lastSweep = level.time;
			TArray<AActor *> stale;
			TMap<AActor *, TintState>::Iterator it( g_state );
			TMap<AActor *, TintState>::Pair *pair;
			while ( it.NextPair( pair ))
				if ( level.time - pair->Value.lastTic > 70 )
					stale.Push( pair->Key );
			for ( unsigned int i = 0; i < stale.Size(); ++i )
				g_state.Remove( stale[i] );
		}
		return st.intensity;
	}

	// Shared qualification + strength for both hooks. Returns the effective percent (0 = no tint)
	// and the floor's average color.
	int TintStrength( AActor *actor, int blendOp, DWORD styleFlags, int basePct, PalEntry &avgOut )
	{
		if ( basePct <= 0 || gamestate != GS_LEVEL || actor == NULL )
			return 0;
		if ( actor->player == NULL || actor->player->mo != actor )
			return 0; // players only: floor damage is a player mechanic
		if ( blendOp != STYLEOP_Add || ( styleFlags & STYLEF_ColorIsFixed ))
			return 0; // stencil/shaded/fuzz/subtractive styles own their look

		// Mirror P_PlayerInSpecialSector's gate EXACTLY: the damage applies only when the player's
		// z sits on THIS sector's own floor plane (or they're in its water). floorz would be wrong
		// here -- it is the highest touching floor across the radius, so hanging onto a ledge over
		// nukage would tint without damaging.
		sector_t *sec = actor->Sector;
		bool onSectorFloor = sec != NULL
			&& ( actor->z == sec->floorplane.ZatPoint( actor->x, actor->y ) || actor->waterlevel );
		bool active = onSectorFloor
			&& FloorDamageAmount( sec ) > 0
			&& !SuitProtected( actor->player );

		float intensity = UpdateIntensity( actor, active );
		if ( intensity <= 0.02f )
			return 0;

		float pulse = active ? PulseFactor( level.time & 31, PULSE_PEAK, PULSE_DECAY ) : 1.0f;
		int pct = EffectivePct( basePct, intensity, pulse );
		if ( pct > 0 )
			avgOut = FloorAverageColor( sec->GetTexture( sector_t::floor ));
		return pct;
	}
} // namespace

bool DamageTint_BeginSpriteGlow( AActor *actor, int blendOp, DWORD styleFlags )
{
	PalEntry avg;
	int pct = TintStrength( actor, blendOp, styleFlags, cl_damagetint, avg );
	if ( pct <= 0 )
		return false;
	float reach = CoverageSpan( cl_damagetint_coverage, FIXED2FLOAT( actor->height ));
	if ( reach <= 0.0f )
		return false;

	// Emissive gradient from the actor's own floor plane: full tint at the feet, gone `reach`
	// units up (coverage % of the body). The shader's glow path does the per-pixel falloff.
	float s = pct / 100.0f;
	float top[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // alpha 0 disables the ceiling side
	float bot[4] = { avg.r / 255.0f * s, avg.g / 255.0f * s, avg.b / 255.0f * s, reach };
	gl_RenderState.EnableGlow( true );
	gl_RenderState.SetGlowParams( top, bot );
	gl_RenderState.SetGlowPlanes( actor->Sector->ceilingplane, actor->Sector->floorplane );
	return true;
}

void DamageTint_EndSpriteGlow()
{
	gl_RenderState.EnableGlow( false );
}

bool DamageTint_WeaponParams( AActor *playermo, int blendOp, DWORD styleFlags, PalEntry &avgOut, int &pctOut, float &coverageFracOut )
{
	pctOut = TintStrength( playermo, blendOp, styleFlags, cl_damagetint_weapon, avgOut );
	if ( pctOut <= 0 )
		return false;
	int cov = clamp<int>( cl_damagetint_weapon_coverage, 0, 100 );
	if ( cov <= 0 )
		return false;
	coverageFracOut = cov / 100.0f;
	return true;
}

PalEntry DamageTint_Blend( PalEntry avg, int pct )
{
	return PalEntry( 255, (BYTE)TintChannel( avg.r, pct ), (BYTE)TintChannel( avg.g, pct ), (BYTE)TintChannel( avg.b, pct ));
}

void DamageTint_Arm2D( PalEntry overlay, float coverageFrac )
{
	if ( overlay.a == 0 || coverageFrac <= 0.0f )
		return;
	// Texture-V runs 0 (top) to 1 (bottom) on a 2D graphic: gradient bottom = 1, top = 1 - coverage.
	gl_RenderState.SetDamageTint( overlay.r / 255.0f, overlay.g / 255.0f, overlay.b / 255.0f,
		overlay.a / 255.0f, 1.0f - coverageFrac, 1.0f );
}

void DamageTint_Disarm2D()
{
	gl_RenderState.ClearDamageTint();
}

PalEntry DamageTint_FaceOverlay( float *coverageFracOut )
{
	if ( coverageFracOut != NULL )
		*coverageFracOut = 0.0f;
	if ( consoleplayer < 0 || consoleplayer >= MAXPLAYERS || !playeringame[consoleplayer] )
		return PalEntry( 0 );
	AActor *mo = players[consoleplayer].mo;
	PalEntry avg;
	// The mugshot has no render style; qualify with the neutral style so only the floor state decides.
	int pct = TintStrength( mo, STYLEOP_Add, 0, cl_damagetint_face, avg );
	if ( pct <= 0 )
		return PalEntry( 0 );
	int cov = clamp<int>( cl_damagetint_face_coverage, 0, 100 );
	if ( cov <= 0 )
		return PalEntry( 0 );
	if ( coverageFracOut != NULL )
		*coverageFracOut = cov / 100.0f;
	return PalEntry( (BYTE)( pct * 255 / 100 ), avg.r, avg.g, avg.b );
}

#else // NO_GL -- dedicated server: no renderer, no tint; nothing references these there.

#include "features/damage-tint/damagetint.h"
bool DamageTint_BeginSpriteGlow( AActor *, int, DWORD ) { return false; }
void DamageTint_EndSpriteGlow() {}
bool DamageTint_WeaponParams( AActor *, int, DWORD, PalEntry &, int &, float & ) { return false; }
PalEntry DamageTint_Blend( PalEntry, int ) { return PalEntry( 255, 255, 255, 255 ); }
PalEntry DamageTint_FaceOverlay( float *cov ) { if ( cov ) *cov = 0.0f; return PalEntry( 0 ); }
void DamageTint_Arm2D( PalEntry, float ) {}
void DamageTint_Disarm2D() {}

#endif // NO_GL
