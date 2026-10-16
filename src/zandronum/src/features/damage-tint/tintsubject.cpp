// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] features/damage-tint's game half, reading the live game between tics: the sector damage table, an actor's floor damage and protection, and fua_tintdebug.

#include "doomtype.h"
#include "doomstat.h"
#include "g_level.h"
#include "actor.h"
#include "d_player.h"
#include "r_defs.h"
#include "p_lnspec.h"
#include "p_local.h"
#include "p_terrain.h"
#include "p_3dfloors.h"
#include "a_artifacts.h"
#include "c_dispatch.h"

#include "features/damage-tint/damagetint.h"
#include "features/damage-tint/tintsubject.h"

// ---------------------------------------------------------------------------------------------
// Sim-side (compiled everywhere, including dedicated servers): the sector damage table, exported
// for the bridge's world.sectors query and read by the floor test below.
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

namespace zx { namespace damagetint {

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

TintSubject TintSubjectOf( AActor *actor )
{
	TintSubject s = { actor, false, false, FTextureID() };
	s.texture.SetInvalid();
	if ( actor == NULL || actor->player == NULL || actor->player->mo != actor )
		return s;
	s.playerBody = true;
	s.active = FloorDamage( actor, s.texture ) > 0 && !DamageNegated( actor->player );
	return s;
}

}} // namespace zx::damagetint

#ifndef NO_GL

using namespace zx::damagetint;

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
	float intensity;
	int lastTic;
	if ( DamageTint_Ramp( mo, intensity, lastTic ))
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
			p->Key, p->Key, p->Value, SectorSpecialDamage( &s ), si,
			(int)( s.soundorg[0] >> FRACBITS ), (int)( s.soundorg[1] >> FRACBITS ));
	}
}

#endif // NO_GL
