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

#include "doomtype.h"
#include "doomstat.h"
#include "g_level.h"
#include "actor.h"
#include "d_player.h"
#include "r_defs.h"
#include "r_data/renderstyle.h"
#include "p_lnspec.h"
#include "p_local.h"
#include "p_terrain.h"
#include "p_3dfloors.h"
#include "a_artifacts.h"
#include "c_dispatch.h"
#include "textures/textures.h"
#include "bitmap.h"
#include "tarray.h"
#include "c_cvars.h"

#include "features/damage-tint/computation/damagetint_compute.h"
#include "features/damage-tint/damagetint.h"

using namespace zx::damagetint;

// ---------------------------------------------------------------------------------------------
// Sim-side (compiled everywhere, including dedicated servers): the sector damage table, exported
// for the bridge's world.sectors query and shared by the render hooks below.
// ---------------------------------------------------------------------------------------------

namespace
{
	// How much a sector's special/damage config hurts. Mirrors P_PlayerInSpecialSector completely:
	// the generic damage field, every translated named special (65..255) that damages or feeds the
	// Strife hazard counter, MBF21 death bits, and Boom's generalized damage bits.
	int SectorSpecialDamage( const sector_t *sec )
	{
		if ( sec == NULL )
			return 0;
		if ( sec->damage > 0 )
			return sec->damage;

		int special = sec->special & ~SECRET_MASK;
		if ( special == 0 )
			return 0;

		if ( special >= dLight_Flicker && special <= 255 )
		{
			switch ( special )
			{
			case dDamage_Nukage:
			case dDamage_LavaWimpy:
			case dScroll_EastLavaDamage:
			case sLight_Strobe_Hurt:
				return 5;
			case hDamage_Sludge:
				return 4;
			case dDamage_Hellslime:
				return 10;
			case dDamage_LavaHefty:
				return 8;
			case dDamage_SuperHellslime:
			case dLight_Strobe_Hurt:
			case dDamage_End:
				return 20;
			case sDamage_Hellslime:
				return 2;  // Strife hazard buildup
			case sDamage_SuperHellslime:
				return 4;  // Strife hazard buildup
			case Damage_InstantDeath:
				return 999;
			}
			return 0;
		}

		if ( special & DEATH_MASK )
			return 999; // MBF21 death bits: every variant is lethal

		switch ( special & DAMAGE_MASK )
		{
		case 0x0100: return 5;
		case 0x0200: return 10;
		case 0x0300: return 20;
		}
		return 0;
	}
} // namespace

int DamageTint_SectorDamage( const sector_t *sec )
{
	return SectorSpecialDamage( sec );
}

#ifndef NO_GL

#include "gl/system/gl_system.h"
#include "gl/renderer/gl_renderstate.h"

// Strength (color intensity) and coverage (how far up the sprite the gradient reaches, as a percent
// of the body / weapon-quad height). Either at 0 disables that surface. Face is strength-only.
CVAR( Int, cl_damagetint, 100, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_coverage, 50, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_weapon, 100, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_weapon_coverage, 50, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_face, 65, CVAR_ARCHIVE )
CVAR( Int, cl_damagetint_face_coverage, 50, CVAR_ARCHIVE )

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
		float    intensity;
		int      lastTic;
		PalEntry color; // latched while active, so a fade-out keeps the pool's color
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

	// The complete "is this actor's floor hurting them" check across all four systems: base-sector
	// specials, TERRAIN flat damage, and 3D-floor contact (whose top texture then supplies the tint
	// color). Also owns the on-the-floor gate, mirroring the respective damage code's own tests.
	int FloorDamage( AActor *actor, FTextureID &texOut )
	{
		sector_t *sec = actor->Sector;
		if ( sec == NULL )
			return 0;
		texOut = sec->GetTexture( sector_t::floor );

		if ( actor->z == sec->floorplane.ZatPoint( actor->x, actor->y ) || actor->waterlevel )
		{
			int dmg = SectorSpecialDamage( sec );
			if ( dmg > 0 )
				return dmg;
			// TERRAIN: flat-based damage (Heretic/Hexen lava, custom TERRAIN definitions).
			int terrain = P_GetThingFloorType( actor );
			if ( Terrains[terrain].DamageAmount > 0 )
				return Terrains[terrain].DamageAmount;
			return 0;
		}

		// Off the base floor: standing on (or wading in) a 3D floor? Mirrors
		// P_PlayerOnSpecial3DFloor's contact test; the model sector carries the special.
		if ( sec->e != NULL )
		{
			for ( unsigned int i = 0; i < sec->e->XFloor.ffloors.Size(); ++i )
			{
				F3DFloor *rover = sec->e->XFloor.ffloors[i];
				if ( !( rover->flags & FF_EXISTS ) || ( rover->flags & FF_FIX ))
					continue;
				if ( rover->flags & FF_SOLID )
				{
					if ( actor->z != rover->top.plane->ZatPoint( actor->x, actor->y ))
						continue;
				}
				else
				{
					if ( actor->z > rover->top.plane->ZatPoint( actor->x, actor->y )
						|| actor->z + actor->height < rover->bottom.plane->ZatPoint( actor->x, actor->y ))
						continue;
				}
				int dmg = SectorSpecialDamage( rover->model );
				if ( dmg > 0 )
				{
					if ( rover->top.texture != NULL )
						texOut = *rover->top.texture;
					return dmg;
				}
				break;
			}
		}
		return 0;
	}

	// Any damage-negating state: radsuit (and subclasses), invulnerability power/flag, god mode.
	// The tint doesn't vanish for these -- it switches to the protected GLOW.
	bool DamageNegated( player_t *player )
	{
		if ( player->cheats & CF_GODMODE )
			return true;
		if ( player->mo->flags2 & MF2_INVULNERABLE )
			return true;
		for ( AInventory *item = player->mo->Inventory; item != NULL; item = item->Inventory )
			if ( item->IsKindOf( RUNTIME_CLASS( APowerIronFeet ))
				|| item->IsKindOf( RUNTIME_CLASS( APowerInvulnerable )))
				return true;
		return false;
	}

	// Advance (and occasionally prune) the per-actor ramp. Keyed on the actor pointer; an address
	// reused by a new actor can inherit at most one frame of stale fade-out -- cosmetic only.
	float UpdateIntensity( AActor *actor, bool active, PalEntry activeColor, PalEntry &colorOut )
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
			TArray<AActor *> stale;
			TMap<AActor *, TintState>::Iterator it( g_state );
			TMap<AActor *, TintState>::Pair *pair;
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
	int TintStrength( AActor *actor, int blendOp, DWORD styleFlags, int basePct, PalEntry &avgOut, bool &glowOut )
	{
		glowOut = false;
		if ( basePct <= 0 || gamestate != GS_LEVEL || actor == NULL )
			return 0;
		if ( actor->player == NULL || actor->player->mo != actor )
			return 0; // players only: floor damage is a player mechanic
		if ( blendOp != STYLEOP_Add || ( styleFlags & STYLEF_ColorIsFixed ))
			return 0; // stencil/shaded/fuzz/subtractive styles own their look

		// FloorDamage owns the on-the-floor gates, mirroring each damage system's own test (base
		// floor plane / water / 3D-floor contact) -- ledge-hanging neither damages nor tints.
		FTextureID tintTex;
		tintTex.SetInvalid();
		bool active = FloorDamage( actor, tintTex ) > 0;
		glowOut = active && DamageNegated( actor->player );

		PalEntry activeColor( 255, 255, 255 );
		if ( active )
			activeColor = FloorAverageColor( tintTex );

		float intensity = UpdateIntensity( actor, active, activeColor, avgOut );
		if ( intensity <= 0.02f )
			return 0;

		float pulse = active ? PulseFactor( level.time & 31, PULSE_PEAK, PULSE_DECAY ) : 1.0f;
		return EffectivePct( basePct, intensity, pulse );
	}
} // namespace

bool DamageTint_BeginSpriteGlow( AActor *actor, int blendOp, DWORD styleFlags, float vt, float vb )
{
	PalEntry avg;
	bool glow;
	int pct = TintStrength( actor, blendOp, styleFlags, cl_damagetint, avg, glow );
	if ( pct <= 0 )
		return false;
	int cov = clamp<int>( cl_damagetint_coverage, 0, 100 );
	if ( cov <= 0 )
		return false;

	// Per-pixel gradient across the sprite's vertical texture span: full tint at the feet (the
	// larger V), fading to none at the coverage point. Same shader path as the weapon and
	// mugshot, so it reads identically on any clothing color; protected players glow instead.
	float vBottom = vb > vt ? vb : vt;
	float vTop    = vb > vt ? vt : vb;
	float covFrac = cov / 100.0f;
	gl_RenderState.SetDamageTint( avg.r / 255.0f, avg.g / 255.0f, avg.b / 255.0f,
		pct / 100.0f, vBottom - ( vBottom - vTop ) * covFrac, vBottom, glow );
	return true;
}

void DamageTint_EndSpriteGlow()
{
	gl_RenderState.ClearDamageTint();
}

bool DamageTint_WeaponParams( AActor *playermo, int blendOp, DWORD styleFlags, PalEntry &avgOut, int &pctOut, float &coverageFracOut, bool &glowOut )
{
	pctOut = TintStrength( playermo, blendOp, styleFlags, cl_damagetint_weapon, avgOut, glowOut );
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

void DamageTint_Arm2D( PalEntry overlay, float coverageFrac, bool glow )
{
	if ( overlay.a == 0 || coverageFrac <= 0.0f )
		return;
	// Texture-V runs 0 (top) to 1 (bottom) on a 2D graphic: gradient bottom = 1, top = 1 - coverage.
	gl_RenderState.SetDamageTint( overlay.r / 255.0f, overlay.g / 255.0f, overlay.b / 255.0f,
		overlay.a / 255.0f, 1.0f - coverageFrac, 1.0f, glow );
}

void DamageTint_Disarm2D()
{
	gl_RenderState.ClearDamageTint();
}

// Diagnostic: why is (or isn't) the tint firing right here? Also prints a histogram of every
// nonzero sector special on the map so an uncovered damaging special is immediately visible.
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
		(int)( sec - sectors ), sec->special, sec->special, sec->damage, FloorDamage( mo, dbgTex ),
		terrain, Terrains[terrain].DamageAmount );
	Printf( "z=%d floorz=%d planez=%d onfloor=%d waterlevel=%d protected=%d 3dfloors=%d\n",
		(int)( mo->z >> FRACBITS ), (int)( mo->floorz >> FRACBITS ), (int)( fz >> FRACBITS ),
		(int)( mo->z == fz ), mo->waterlevel, (int)( mo->player && DamageNegated( mo->player )),
		sec->e ? (int)sec->e->XFloor.ffloors.Size() : 0 );
	if ( TintState *st = g_state.CheckKey( mo ))
		Printf( "intensity=%.2f lastTic=%d (now %d)\n", st->intensity, st->lastTic, level.time );
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
			p->Key, p->Key, p->Value, SectorSpecialDamage( &s ), si,
			(int)( s.soundorg[0] >> FRACBITS ), (int)( s.soundorg[1] >> FRACBITS ));
	}
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
	int pct = TintStrength( mo, STYLEOP_Add, 0, cl_damagetint_face, avg, glow );
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
bool DamageTint_WeaponParams( AActor *, int, DWORD, PalEntry &, int &, float &, bool & ) { return false; }
PalEntry DamageTint_Blend( PalEntry, int ) { return PalEntry( 255, 255, 255, 255 ); }
PalEntry DamageTint_FaceOverlay( float *cov, bool *glow ) { if ( cov ) *cov = 0.0f; if ( glow ) *glow = false; return PalEntry( 0 ); }
void DamageTint_Arm2D( PalEntry, float, bool ) {}
void DamageTint_Disarm2D() {}

#endif // NO_GL
