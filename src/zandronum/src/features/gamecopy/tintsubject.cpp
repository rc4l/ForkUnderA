// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] features/damage-tint's game half, read into the render copy between tics: the sector damage table and an actor's floor damage and protection.

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

#include "features/gamecopy/tintsubject.h"

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

namespace zx { namespace gamecopy {

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
	TintSubject s = { actor != NULL ? actor->rcSerial : 0u, false, false, FTextureID() };
	s.texture.SetInvalid();
	if ( actor == NULL || actor->player == NULL || actor->player->mo != actor )
		return s;
	s.playerBody = true;
	s.active = FloorDamage( actor, s.texture ) > 0 && !DamageNegated( actor->player );
	return s;
}

}} // namespace zx::gamecopy

