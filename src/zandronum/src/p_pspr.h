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
#include <limits.h>
#include "tables.h"
#include "tarray.h"
#include "r_data/renderstyle.h"
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
// [overlay] Psprite layers are addressed by id rather than by dense array index,
// so DECORATE A_Overlay can create arbitrary layers. These are the engine-reserved
// ids; overlays use any other id (lower id = drawn first / further back). The
// values mirror GZDoom so mod layer numbers behave the same way.
enum
{
	ps_weapon		= 1,
	ps_flash		= 1000,
	ps_targetcenter	= INT_MAX - 2,
	ps_targetleft	= INT_MAX - 1,
	ps_targetright	= INT_MAX,
};

// [overlay] Count of engine-reserved (non-overlay) layers.
#define NUM_RESERVED_PSPRITES 5

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

// [overlay] PSPF_* per-layer flags (matching GZDoom names/values).
enum
{
	PSPF_ADDWEAPON			= 1 << 0,	// offsets are added to the weapon layer's offsets
	PSPF_ADDBOB				= 1 << 1,	// weapon bob is added to this layer
	PSPF_POWDOUBLE			= 1 << 2,	// tics run at double speed under a powerup
	PSPF_CVARFAST			= 1 << 3,	// obey sv_fastweapons for this layer
	PSPF_ALPHA				= 1 << 4,	// use this layer's alpha
	PSPF_RENDERSTYLE		= 1 << 5,	// use this layer's render style
	PSPF_FORCEALPHA			= 1 << 6,	// alpha even overrides some render styles
	PSPF_FORCESTYLE			= 1 << 7,	// render style overrides even opaque weapons
	PSPF_FLIP				= 1 << 8,	// mirror the sprite pixels horizontally
	PSPF_MIRROR				= 1 << 9,	// mirror the sprite's horizontal offset (not the pixels)
	PSPF_PLAYERTRANSLATED	= 1 << 10,	// apply the player's translation
	PSPF_INTERPOLATE		= 1 << 11,	// smooth this layer's offset between tics
};

// [overlay] WOF_* flags for A_OverlayOffset / A_OverlayScale.
enum
{
	WOF_KEEPX		= 1 << 0,	// leave the X value unchanged
	WOF_KEEPY		= 1 << 1,	// leave the Y value unchanged
	WOF_ADD			= 1 << 2,	// add to the current value instead of setting it
	WOF_INTERPOLATE	= 1 << 3,	// interpolate from the previous value
};

struct pspdef_t
{
	FState*		state;	// a NULL state means not active
	int 		tics;
	fixed_t 	sx;
	fixed_t 	sy;
	int			sprite;
	int			frame;
	bool		processPending; // true: waiting for periodic processing on this tick
	// [overlay] Per-layer state used by the A_Overlay family.
	int			layer;		// this layer's id (see the reserved ids above)
	DWORD		Flags;		// PSPF_* flags
	fixed_t		alpha;		// per-layer alpha (FRACUNIT = opaque)
	FRenderStyle RenderStyle;	// per-layer render style (AsDWORD == 0 means "unset")
	fixed_t		scalex;		// per-layer horizontal scale (FRACUNIT = 1x)
	fixed_t		scaley;		// per-layer vertical scale (FRACUNIT = 1x)
	fixed_t		oldx;		// sx at the start of this tic, for interpolation
	fixed_t		oldy;		// sy at the start of this tic, for interpolation
	bool		bInterpolate;	// interpolate this tic (per-tic, not saved)

	// [overlay] Defaults matter now that layers are created on demand. New overlays follow the
	// weapon offset and bob by default, matching GZDoom (reserved layers ignore these flags).
	pspdef_t()
	: state(NULL), tics(0), sx(0), sy(0), sprite(0), frame(0), processPending(false),
	  layer(0), Flags(PSPF_ADDWEAPON | PSPF_ADDBOB), alpha(FRACUNIT),
	  scalex(FRACUNIT), scaley(FRACUNIT), oldx(0), oldy(0), bInterpolate(false)
	{
		RenderStyle.AsDWORD = 0;
	}
};

class FArchive;

FArchive &operator<< (FArchive &, pspdef_t &);

// [rc4l] Id-addressed psprite layers, mirroring the arbitrary-layer model GZDoom introduced
// with DPSprite, but implemented here as a container over this engine's pspdef_t rather than a
// port of GZDoom's GC thinker list.
// [overlay] Owns the player's psprite layers. Nodes are heap-allocated so that
// pointers taken with operator[] stay valid across A_Overlay calls that add new
// layers mid-tick (as P_SetPsprite does while dispatching a state's action).
struct FPSpriteLayers
{
	TArray<pspdef_t *>	list;	// sorted ascending by ->layer; this owns the nodes

	FPSpriteLayers();
	FPSpriteLayers(const FPSpriteLayers &other);
	FPSpriteLayers &operator=(const FPSpriteLayers &other);
	~FPSpriteLayers();

	// Find-or-create by layer id; keeps source compatibility with psprites[ps_weapon].
	pspdef_t &operator[](int layer);
	pspdef_t *Find(int layer) const;			// NULL if the layer is not active
	void RemoveLayer(int layer);				// no-op on reserved layers
	void ClearRange(int start, int stop, bool safety);
	void ClearOverlays();						// remove every non-reserved layer
	void ResetToReserved();						// drop everything, recreate the 5 reserved (inactive)

	unsigned int Size() const { return list.Size(); }
	pspdef_t &Element(unsigned int i) { return *list[i]; }
	const pspdef_t &Element(unsigned int i) const { return *list[i]; }

private:
	pspdef_t *CreateSorted(int layer);
	void DeleteAll();
};

FArchive &operator<< (FArchive &, FPSpriteLayers &);

// [overlay] True for the engine-owned reserved layers (weapon/flash/targeters).
bool P_IsReservedPSpriteLayer(int layer);

// [overlay] The psprite layer whose state action is currently executing (0 if none).
int P_GetCurrentPSpriteLayer();

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
