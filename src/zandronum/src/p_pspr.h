//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1994-1996 Raven Software
// Copyright 1999-2016 Marisa Heit
// Copyright 2002-2016 Christoph Oelckers
// Copyright 2012-2024 Zandronum Development Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
// DESCRIPTION:
//	Sprite animation.
//
//-----------------------------------------------------------------------------


#ifndef __P_PSPR_H__
#define __P_PSPR_H__

// Basic data types.
// Needs fixed point, and BAM angles.
#include "tables.h"
#include "thingdef/thingdef.h"

#define WEAPONBOTTOM			128*FRACUNIT

// [RH] +0x6000 helps it meet the screen bottom
//		at higher resolutions while still being in
//		the right spot at 320x200.
#define WEAPONTOP				(32*FRACUNIT+0x6000)


//
// Overlay psprites are scaled shapes
// drawn directly on the view screen,
// coordinates are given for a 320*200 view screen.
//
typedef enum
{
	ps_weapon,
	ps_flash,
	ps_targetcenter,
	ps_targetleft,
	ps_targetright,
	NUMPSPRITES

} psprnum_t;

// [AK] Enums for all weapon sway styles (used for cl_swaystyle).
enum
{
	WEAPON_SWAY_NORMAL,
	WEAPON_SWAY_DOWNONLY,
	WEAPON_SWAY_UPONLY,
	WEAPON_SWAY_HORIZONTALONLY,
};

// [AK] Enums for all weapon pitch offset styles (used for cl_viewpitchstyle).
enum
{
	WEAPON_PITCH_FULL,
	WEAPON_PITCH_UPONLY,
	WEAPON_PITCH_DOWNONLY,
	WEAPON_PITCH_DOWNANDUP,
	WEAPON_PITCH_CENTERED,
};

/*
inline FArchive &operator<< (FArchive &arc, psprnum_t &i)
{
	BYTE val = (BYTE)i;
	arc << val;
	i = (psprnum_t)val;
	return arc;
}
*/

struct pspdef_t
{
	FState*		state;	// a NULL state means not active
	int 		tics;
	fixed_t 	sx;
	fixed_t 	sy;
	int			sprite;
	int			frame;
	bool		processPending; // true: waiting for periodic processing on this tick
};

class FArchive;

FArchive &operator<< (FArchive &, pspdef_t &);

class player_t;
class AActor;
struct FState;

void P_NewPspriteTick(player_t *player = NULL); // [EP] Add player parameter.
void P_SetPsprite (player_t *player, int position, FState *state, bool nofunction=false);
void P_CalcSwing (player_t *player);
void P_BringUpWeapon (player_t *player);
void P_FireWeapon (player_t *player);
void P_DropWeapon (player_t *player);
void P_BobWeapon (player_t *player, pspdef_t *psp, fixed_t *x, fixed_t *y);
angle_t P_BulletSlope (AActor *mo, AActor **pLineTarget = NULL);
void P_GunShot (AActor *mo, bool accurate, const PClass *pufftype, angle_t pitch);

void DoReadyWeapon(AActor * self);
void DoReadyWeaponToBob(AActor * self);
void DoReadyWeaponToFire(AActor * self, bool primary = true, bool secondary = true);
void DoReadyWeaponToSwitch(AActor * self, bool switchable = true);

DECLARE_ACTION(A_Raise)
void A_ReFire(AActor *self, FState *state = NULL);
// [BB] ST also needs A_GunFlash.
void A_GunFlash(AActor *self, FState *flash = NULL, const int Flags = 0);

#endif	// __P_PSPR_H__
