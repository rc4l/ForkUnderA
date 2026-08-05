
//**************************************************************************
//**
//** p_pspr.c : Heretic 2 : Raven Software, Corp.
//**
//** $RCSfile: p_pspr.c,v $
//** $Revision: 1.105 $
//** $Date: 96/01/06 03:23:35 $
//** $Author: bgokey $
//**
//**************************************************************************

// HEADER FILES ------------------------------------------------------------

#include <stdlib.h>

#include "doomdef.h"
#include "d_event.h"
#include "c_cvars.h"
#include "m_random.h"
#include "p_enemy.h"
#include "p_local.h"
#include "s_sound.h"
#include "doomstat.h"
#include "gi.h"
#include "p_pspr.h"
#include "templates.h"
#include "thingdef/thingdef.h"
#include "g_level.h"
#include "farchive.h"
#include "d_player.h"
// [BB] New #includes.
#include "deathmatch.h"
#include "network.h"
#include "cl_demo.h"
#include "p_effect.h"
#include "sv_commands.h"
#include "unlagged.h"
#include "g_game.h"
#include "p_tick.h"
#include "cl_main.h"
#include "computation/psprite_overlay_compute.h"


// MACROS ------------------------------------------------------------------

#define LOWERSPEED				FRACUNIT*6
#define RAISESPEED				FRACUNIT*6

// [CK] The minimum binary angle for autoaim to trigger against other players.
// This was determined by making a triangle from the max autoaim range (1024) by
// player radius (16) to get an angle of ~0.89 degrees. This below value is the
// binary value of this angle.
#define AUTOAIM_MINANGLE		0xA20500

// TYPES -------------------------------------------------------------------

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// [SO] 1=Weapons states are all 1 tick
//		2=states with a function 1 tick, others 0 ticks.
// [AK] Added CVAR_GAMEPLAYSETTING.
CUSTOM_CVAR( Int, sv_fastweapons, 0, CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING )
{
	if ( self >= 3 )
		self = 2;
	if ( self < 0 )
		self = 0;

	// [AK] Notify the clients about the change.
	SERVER_SettingChanged( self, false );
}

// [AK] CVars that control how the weapon bobs.
CVAR( Bool, cl_alwaysbob, false, CVAR_ARCHIVE )
CVAR( Bool, cl_usecustombob, false, CVAR_ARCHIVE )
CVAR( Float, cl_bobspeed, 1.0f, CVAR_ARCHIVE )
CVAR( Float, cl_stillbobspeed, 0.0f, CVAR_ARCHIVE )
CVAR( Float, cl_stillbobrange, 0.0f, CVAR_ARCHIVE )

// [AK] CVars that control how the weapon sways.
CVAR( Bool, cl_usecustomsway, false, CVAR_ARCHIVE )
CVAR( Float, cl_viewswayspeed, 0.0f, CVAR_ARCHIVE )
CVAR( Float, cl_motionswayspeed, 0.0f, CVAR_ARCHIVE )
CVAR( Float, cl_jumpswayspeed, 0.0f, CVAR_ARCHIVE )

// [AK] CVars that control how the weapon offsets based on the player's pitch.
CVAR( Bool, cl_usecustompitch, false, CVAR_ARCHIVE )
CVAR( Float, cl_viewpitchoffset, 0.0f, CVAR_ARCHIVE )

// [AK] Allows a different bob style to be used than what the weapon uses.
CUSTOM_CVAR( Int, cl_bobstyle, AWeapon::BobNormal, CVAR_ARCHIVE )
{
	if (self < AWeapon::BobNormal)
		self = AWeapon::BobNormal;
	else if ( self > AWeapon::BobQuake )
		self = AWeapon::BobQuake;
}

// [AK] Specifies how to sway the weapon depending on how the player looks around.
CUSTOM_CVAR( Int, cl_swaystyle, WEAPON_SWAY_NORMAL, CVAR_ARCHIVE )
{
	if (self < WEAPON_SWAY_NORMAL)
		self = WEAPON_SWAY_NORMAL;
	else if (self > WEAPON_SWAY_HORIZONTALONLY)
		self = WEAPON_SWAY_HORIZONTALONLY;
}

// [AK] Controls which parts of the player's pitch affect the offset of the weapon and how.
CUSTOM_CVAR( Int, cl_viewpitchstyle, WEAPON_PITCH_FULL, CVAR_ARCHIVE )
{
	if (self < WEAPON_PITCH_FULL)
		self = WEAPON_PITCH_FULL;
	else if (self > WEAPON_PITCH_CENTERED)
		self = WEAPON_PITCH_CENTERED;
}

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static FRandom pr_wpnreadysnd ("WpnReadySnd");
static FRandom pr_gunshot ("GunShot");

// CODE --------------------------------------------------------------------

// [overlay] Keep the ps_* reserved ids in lockstep with the compute module's canonical
// values; these fire at compile time if either side is edited without the other.
static_assert(ps_weapon == ZX_PSP_WEAPON, "ps_weapon id mismatch with compute module");
static_assert(ps_flash == ZX_PSP_FLASH, "ps_flash id mismatch with compute module");
static_assert(ps_targetcenter == ZX_PSP_TARGETCENTER, "ps_targetcenter id mismatch with compute module");
static_assert(ps_targetleft == ZX_PSP_TARGETLEFT, "ps_targetleft id mismatch with compute module");
static_assert(ps_targetright == ZX_PSP_TARGETRIGHT, "ps_targetright id mismatch with compute module");
// [overlay] The pure draw-offset helper hardcodes these two PSPF_ bits; keep them in sync.
static_assert(PSPF_ADDWEAPON == ZX_PSPF_ADDWEAPON, "PSPF_ADDWEAPON mismatch with compute module");
static_assert(PSPF_ADDBOB == ZX_PSPF_ADDBOB, "PSPF_ADDBOB mismatch with compute module");

bool P_IsReservedPSpriteLayer(int layer)
{
	return ComputeIsReservedPSpriteLayer(layer);
}

// [overlay] The layer whose state action is currently executing, so the A_Overlay family can
// resolve a layer argument of 0 to "the calling layer" (and back OverlayID()). Saved/restored
// re-entrantly around each state dispatch in P_SetPsprite.
static int g_currentPSpriteLayer = 0;

int P_GetCurrentPSpriteLayer()
{
	return g_currentPSpriteLayer;
}

//---------------------------------------------------------------------------
//
// [overlay] FPSpriteLayers - the player's dynamic set of psprite layers.
//
//---------------------------------------------------------------------------

FPSpriteLayers::FPSpriteLayers()
{
	ResetToReserved();
}

FPSpriteLayers::FPSpriteLayers(const FPSpriteLayers &other)
{
	*this = other;
}

FPSpriteLayers &FPSpriteLayers::operator=(const FPSpriteLayers &other)
{
	if (this == &other)
		return *this;

	DeleteAll();
	for (unsigned int i = 0; i < other.list.Size(); i++)
		list.Push(new pspdef_t(*other.list[i]));

	return *this;
}

FPSpriteLayers::~FPSpriteLayers()
{
	DeleteAll();
}

void FPSpriteLayers::DeleteAll()
{
	for (unsigned int i = 0; i < list.Size(); i++)
		delete list[i];
	list.Clear();
}

pspdef_t *FPSpriteLayers::CreateSorted(int layer)
{
	pspdef_t *node = new pspdef_t;
	node->layer = layer;

	// Gather the current ids so the insert position is decided by the pure helper.
	TArray<int> ids;
	for (unsigned int i = 0; i < list.Size(); i++)
		ids.Push(list[i]->layer);

	const int *idptr = ids.Size() ? &ids[0] : NULL;
	unsigned int index = ComputeSortedInsertIndex(idptr, ids.Size(), layer);
	list.Insert(index, node);

	return node;
}

pspdef_t *FPSpriteLayers::Find(int layer) const
{
	for (unsigned int i = 0; i < list.Size(); i++)
		if (list[i]->layer == layer)
			return list[i];

	return NULL;
}

pspdef_t &FPSpriteLayers::operator[](int layer)
{
	pspdef_t *p = Find(layer);
	if (p == NULL)
		p = CreateSorted(layer);

	return *p;
}

void FPSpriteLayers::RemoveLayer(int layer)
{
	// Reserved layers are engine-owned and never destroyed, only deactivated.
	if (P_IsReservedPSpriteLayer(layer))
		return;

	for (unsigned int i = 0; i < list.Size(); i++)
	{
		if (list[i]->layer == layer)
		{
			delete list[i];
			list.Delete(i);
			return;
		}
	}
}

void FPSpriteLayers::ClearRange(int start, int stop, bool safety)
{
	for (unsigned int i = 0; i < list.Size(); )
	{
		if (ComputeClearOverlayShouldRemove(list[i]->layer, start, stop, safety))
		{
			delete list[i];
			list.Delete(i);
		}
		else
		{
			i++;
		}
	}
}

void FPSpriteLayers::ClearOverlays()
{
	// Remove every non-reserved layer (start==stop==0 means "all"; safety keeps the reserved).
	ClearRange(0, 0, true);
}

void FPSpriteLayers::ResetToReserved()
{
	DeleteAll();

	static const int reserved[NUM_RESERVED_PSPRITES] =
		{ ps_weapon, ps_flash, ps_targetcenter, ps_targetleft, ps_targetright };
	for (int i = 0; i < NUM_RESERVED_PSPRITES; i++)
		CreateSorted(reserved[i]);
}

//---------------------------------------------------------------------------
//
// PROC P_NewPspriteTick
//
//---------------------------------------------------------------------------

// [EP] Added player parameter.
void P_NewPspriteTick(player_t *player) 
{
	// This function should be called after the beginning of a tick, before any possible
	// prprite-event, or near the end, after any possible psprite event.
	// Because data is reset for every tick (which it must be) this has no impact on savegames.
	for (int i = 0; i<MAXPLAYERS; i++)
	{
		// [EP] if player is not NULL, only this player's psprite settings are changed.
		if (playeringame[i] && ( player == NULL || player-players == i ))
		{
			// [overlay] Mark every active layer (weapon, flash, overlays, targeter).
			FPSpriteLayers &pspr = players[i].psprites;
			for (unsigned int j = 0; j < pspr.Size(); j++)
			{
				pspr.Element(j).processPending = true;
			}
		}
	}
}

//---------------------------------------------------------------------------
//
// PROC P_SetPsprite
//
//---------------------------------------------------------------------------

void P_SetPsprite (player_t *player, int position, FState *state, bool nofunction)
{
	pspdef_t *psp;

	if (position == ps_weapon && !nofunction)
	{ // A_WeaponReady will re-set these as needed
		player->WeaponState &= ~(WF_WEAPONREADY | WF_WEAPONREADYALT | WF_WEAPONBOBBING | WF_WEAPONSWITCHOK | WF_WEAPONRELOADOK | WF_WEAPONZOOMOK);
	}

	psp = &player->psprites[position];
	psp->processPending = false; // Do not subsequently perform periodic processing within the same tick.

	do
	{
		if (state == NULL)
		{ // Object removed itself.
			psp->state = NULL;
			break;
		}
		psp->state = state;

		if (state->sprite != SPR_FIXED)
		{ // okay to change sprite and/or frame
			if (!state->GetSameFrame())
			{ // okay to change frame
				psp->frame = state->GetFrame();
			}
			if (state->sprite != SPR_NOCHANGE)
			{ // okay to change sprite
				psp->sprite = state->sprite;
			}
		}


		// [overlay] Reserved layers obey sv_fastweapons as before; overlays only if CVARFAST.
		bool fasteligible = P_IsReservedPSpriteLayer(position) || (psp->Flags & PSPF_CVARFAST);
		if (sv_fastweapons >= 2 && position == ps_weapon)
			psp->tics = state->ActionFunc == NULL? 0 : 1;
		else if (sv_fastweapons && fasteligible)
			psp->tics = 1;		// great for producing decals :)
		else
			psp->tics = state->GetTics(); // could be 0

		if (state->GetMisc1())
		{ // Set coordinates.
			psp->sx = state->GetMisc1()<<FRACBITS;
		}
		if (state->GetMisc2())
		{
			psp->sy = state->GetMisc2()<<FRACBITS;
		}

		// [BB] Some action functions rely on the fact that ReadyWeapon is not NULL.
		if (!nofunction && player->mo != NULL && player->ReadyWeapon)
		{
			// [overlay] Track the executing layer so A_Overlay(0, ...) / OverlayID() resolve.
			int savedLayer = g_currentPSpriteLayer;
			g_currentPSpriteLayer = position;
			bool actionResult = state->CallAction(player->mo, player->ReadyWeapon);
			g_currentPSpriteLayer = savedLayer;

			// [overlay] The action may have destroyed this very layer (e.g. A_ClearOverlays);
			// re-fetch it and stop if it is gone.
			psp = player->psprites.Find(position);
			if (psp == NULL)
				break;

			if (actionResult && !psp->state)
			{
				break;
			}
		}

		state = psp->state->GetNextState();
	} while (!psp->tics); // An initial state of 0 could cycle through.

	// [overlay] An overlay whose state chain ended removes itself; reserved layers persist.
	if (psp != NULL && psp->state == NULL && !P_IsReservedPSpriteLayer(position))
		player->psprites.RemoveLayer(position);
}

//---------------------------------------------------------------------------
//
// PROC P_BringUpWeapon
//
// Starts bringing the pending weapon up from the bottom of the screen.
// This is only called to start the rising, not throughout it.
//
//---------------------------------------------------------------------------

void P_BringUpWeapon (player_t *player)
{
	FState *newstate;
	AWeapon *weapon;

	if (player->PendingWeapon == WP_NOCHANGE)
	{
		if (player->ReadyWeapon != NULL)
		{
			player->psprites[ps_weapon].sy = WEAPONTOP;
			P_SetPsprite (player, ps_weapon, player->ReadyWeapon->GetReadyState());
		}
		return;
	}

	weapon = player->PendingWeapon;

	// If the player has a tome of power, use this weapon's powered up
	// version, if one is available.
	if (weapon != NULL &&
		weapon->SisterWeapon &&
		weapon->SisterWeapon->WeaponFlags & WIF_POWERED_UP &&
		player->mo->FindInventory (RUNTIME_CLASS(APowerWeaponLevel2), true))
	{
		weapon = weapon->SisterWeapon;
	}

	if (weapon != NULL)
	{
		if (weapon->UpSound)
		{
			S_Sound (player->mo, CHAN_WEAPON, weapon->UpSound, 1, ATTN_NORM);
		}
		newstate = weapon->GetUpState ();
		player->refire = 0;
	}
	else
	{
		newstate = NULL;
	}

	// [AK] Save the last weapon the player was using before selecting the pending weapon.
	if (( NETWORK_GetState() != NETSTATE_SERVER ) && ( player->ReadyWeapon != NULL ))
		LastWeaponUsed = player->ReadyWeapon->GetClass();

	player->PendingWeapon = WP_NOCHANGE;
	player->ReadyWeapon = weapon;
	// [overlay] Drop any overlays left over from the previous weapon before the new one's
	// select state runs (which may create its own overlays).
	player->psprites.ClearOverlays();
	player->psprites[ps_weapon].sy = player->cheats & CF_INSTANTWEAPSWITCH
		? WEAPONTOP : WEAPONBOTTOM;
	P_SetPsprite (player, ps_weapon, newstate);
	// [rc4l] uzdoom@261bc7784: make sure that the previous weapon's flash state is terminated.
	// When coming here from a weapon drop it may still be active. ClearOverlays() above does not
	// cover this -- ps_flash is a reserved layer and that call only removes non-reserved ones.
	// Ungated on purpose, mirroring the P_SetPsprite right above it: psprites are per-player
	// presentation state that both ends run locally, with no SERVERCOMMANDS behind them.
	P_SetPsprite(player, ps_flash, NULL);
	// [rc4l] uzdoom@ee6e87d94: clear the weapon scratch counter when bringing a weapon up.
	player->mo->weaponspecial = 0;
}


//---------------------------------------------------------------------------
//
// PROC P_FireWeapon
//
//---------------------------------------------------------------------------

void P_FireWeapon (player_t *player, FState *state)
{
	AWeapon *weapon;

	// [SO] 9/2/02: People were able to do an awful lot of damage
	// when they were observers...
/* [BB] Zandronum doesn't use ZDoom's bot code.
	if (!player->isbot && bot_observer)
	{
		return;
	}
*/

	weapon = player->ReadyWeapon;
	if (weapon == NULL || !weapon->CheckAmmo (AWeapon::PrimaryFire, true))
	{
		// [BC] We need to do this, otherwise with the BFG10K, you can fire,
		// run out of ammo, find new ammo, switch back, and fire without
		// charging back up.
		player->refire = false;
		return;
	}

	// [BC] If we're the server, tell clients to update this player's state.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_SetPlayerState( ULONG( player - players ), STATE_PLAYER_ATTACK, ULONG( player - players ), SVCF_SKIPTHISCLIENT );

	// [BB] Except for the consoleplayer, the server handles this.
	if (( NETWORK_InClientMode() == false ) ||
		(( player - players ) == consoleplayer ))
	{
		player->mo->PlayAttacking ();
	}

	weapon->bAltFire = false;
	if (state == NULL)
	{
		state = weapon->GetAtkState(!!player->refire);
	}
	P_SetPsprite (player, ps_weapon, state);
	if (!(weapon->WeaponFlags & WIF_NOALERT))
	{
		P_NoiseAlert (player->mo, player->mo, false);
	}
}

//---------------------------------------------------------------------------
//
// PROC P_FireWeaponAlt
//
//---------------------------------------------------------------------------

void P_FireWeaponAlt (player_t *player, FState *state)
{
	AWeapon *weapon;

/* [BB] Zandronum doesn't use ZDoom's bot code.
	// [SO] 9/2/02: People were able to do an awful lot of damage
	// when they were observers...
	if (!player->isbot && bot_observer)
	{
		return;
	}
*/

	weapon = player->ReadyWeapon;
	if (weapon == NULL || weapon->FindState(NAME_AltFire) == NULL || !weapon->CheckAmmo (AWeapon::AltFire, true))
	{
		return;
	}

	// [BB] If we're the server, tell clients to update this player's state.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_SetPlayerState( ULONG( player - players ), STATE_PLAYER_ATTACK_ALTFIRE, ULONG( player - players ), SVCF_SKIPTHISCLIENT );

	// [BB] Except for the consoleplayer, the server handles this.
	if (( NETWORK_InClientMode() == false ) ||
		(( player - players ) == consoleplayer ))
	{
		player->mo->PlayAttacking ();
	}
	weapon->bAltFire = true;

	if (state == NULL)
	{
		state = weapon->GetAltAtkState(!!player->refire);
	}

	P_SetPsprite (player, ps_weapon, state);
	if (!(weapon->WeaponFlags & WIF_NOALERT))
	{
		P_NoiseAlert (player->mo, player->mo, false);
	}
}

//---------------------------------------------------------------------------
//
// PROC P_ReloadWeapon
//
//---------------------------------------------------------------------------

void P_ReloadWeapon (player_t *player, FState *state)
{
	AWeapon *weapon;
	/* [BB] Zandronum doesn't use ZDoom's bot code.
	if (!player->isbot && bot_observer)
	{
		return;
	}
	*/

	weapon = player->ReadyWeapon;
	if (weapon == NULL)
	{
		return;
	}

	if (state == NULL)
	{
		state = weapon->GetRelState();
	}
	// [XA] don't change state if still null, so if the modder sets 
	// WRF_RELOAD to true but forgets to define the Reload state, the weapon
	// won't disappear. ;)
	if (state != NULL)
		P_SetPsprite (player, ps_weapon, state);
}

//---------------------------------------------------------------------------
//
// PROC P_ZoomWeapon
//
//---------------------------------------------------------------------------

void P_ZoomWeapon (player_t *player, FState *state)
{
	AWeapon *weapon;
	/* [BB] Zandronum doesn't use ZDoom's bot code.
	if (!player->isbot && bot_observer)
	{
		return;
	}
	*/

	weapon = player->ReadyWeapon;
	if (weapon == NULL)
	{
		return;
	}

	if (state == NULL)
	{
		state = weapon->GetZoomState();
	}
	// [XA] don't change state if still null. Same reasons as above.
	if (state != NULL)
		P_SetPsprite (player, ps_weapon, state);
}

//---------------------------------------------------------------------------
//
// PROC P_DropWeapon
//
// The player died, so put the weapon away.
//
//---------------------------------------------------------------------------

void P_DropWeapon (player_t *player)
{
	if (player == NULL)
	{
		return;
	}
	// [overlay] The weapon is going away (drop or death); its overlays go with it.
	player->psprites.ClearOverlays();
	// Since the weapon is dropping, stop blocking switching.
	player->WeaponState &= ~WF_DISABLESWITCH;
	if (player->ReadyWeapon != NULL)
	{
		P_SetPsprite (player, ps_weapon, player->ReadyWeapon->GetDownState());
	}
}

//============================================================================
//
// P_BobWeapon
//
// [RH] Moved this out of A_WeaponReady so that the weapon can bob every
// tic and not just when A_WeaponReady is called. Not all weapons execute
// A_WeaponReady every tic, and it looks bad if they don't bob smoothly.
//
// [XA] Added new bob styles and exposed bob properties. Thanks, Ryan Cordell!
//
//============================================================================

void P_BobWeapon (player_t *player, pspdef_t *psp, fixed_t *x, fixed_t *y)
{
	static fixed_t curbob;

	AWeapon *weapon;
	fixed_t bobtarget;

	// [BC] Don't bob weapon if the player is spectating.
	if ( player->bSpectating )
		return;

	weapon = player->ReadyWeapon;

	if (weapon == NULL || weapon->WeaponFlags & WIF_DONTBOB)
	{
		*x = *y = 0;
		return;
	}

	// [XA] Get the current weapon's bob properties.
	// [AK] Adjust the bob style and speed to the client's preference if cl_usecustombob is enabled.
	int bobstyle = cl_usecustombob ? cl_bobstyle : weapon->BobStyle;
	int bobspeed = (int)(((cl_usecustombob ? FLOAT2FIXED(cl_bobspeed) : weapon->BobSpeed) * 128) >> 16);
	fixed_t rangex = weapon->BobRangeX;
	fixed_t rangey = weapon->BobRangeY;

	// Bob the weapon based on movement speed.
	int angle = (bobspeed*35/TICRATE*level.time)&FINEMASK;

	// [RH] Smooth transitions between bobbing and not-bobbing frames.
	// This also fixes the bug where you can "stick" a weapon off-center by
	// shooting it when it's at the peak of its swing.
	// [AK] Keep bobbing the weapon while firing if cl_alwaysbob is enabled.
	bobtarget = ((player->WeaponState & WF_WEAPONBOBBING) || (cl_alwaysbob)) ? player->bob : 0;
	if (curbob != bobtarget)
	{
		if (abs (bobtarget - curbob) <= 1*FRACUNIT)
		{
			curbob = bobtarget;
		}
		else
		{
			fixed_t zoom = MAX<fixed_t> (1*FRACUNIT, abs (curbob - bobtarget) / 40);
			if (curbob > bobtarget)
			{
				curbob -= zoom;
			}
			else
			{
				curbob += zoom;
			}
		}
	}

	if (curbob != 0)
	{
		fixed_t bobx = FixedMul(player->bob, rangex);
		fixed_t boby = FixedMul(player->bob, rangey);
		switch (bobstyle)
		{
		case AWeapon::BobNormal:
			*x = FixedMul(bobx, finecosine[angle]);
			*y = FixedMul(boby, finesine[angle & (FINEANGLES/2-1)]);
			break;
			
		case AWeapon::BobInverse:
			*x = FixedMul(bobx, finecosine[angle]);
			*y = boby - FixedMul(boby, finesine[angle & (FINEANGLES/2-1)]);
			break;
			
		case AWeapon::BobAlpha:
			*x = FixedMul(bobx, finesine[angle]);
			*y = FixedMul(boby, finesine[angle & (FINEANGLES/2-1)]);
			break;
			
		case AWeapon::BobInverseAlpha:
			*x = FixedMul(bobx, finesine[angle]);
			*y = boby - FixedMul(boby, finesine[angle & (FINEANGLES/2-1)]);
			break;
			
		case AWeapon::BobSmooth:
			*x = FixedMul(bobx, finecosine[angle]);
			*y = (boby - FixedMul(boby, finecosine[angle*2 & (FINEANGLES-1)])) / 2;
			break;

		case AWeapon::BobInverseSmooth:
			*x = FixedMul(bobx, finecosine[angle]);
			*y = (FixedMul(boby, finecosine[angle*2 & (FINEANGLES-1)]) + boby) / 2;
			break;

		// [AK] Quake-styled bobbing originally made by Dark-Assassin.
		case AWeapon::BobQuake:
			*x = 0;
			*y = FixedMul(boby, finesine[angle & (FINEANGLES/2-1)]);
			break;
		}
	}
	else
	{
		*x = 0;
		*y = 0;
	}

	const fixed_t stillBobRange = (cl_usecustombob ? FLOAT2FIXED(cl_stillbobrange) : weapon->StillBobRange) - bobtarget;

	// [AK] Also bob the weapon up and down while the player's standing still.
	// Make the transition between "still" and "regular" bobbing smooth.
	if (((player->WeaponState & WF_WEAPONBOBBING) || (cl_alwaysbob)) && (stillBobRange > 0))
	{
		const fixed_t stillBobSpeed = ((cl_usecustombob ? FLOAT2FIXED(cl_stillbobspeed) : weapon->StillBobSpeed) * 128) >> FRACBITS;
		const fixed_t stillBobAngle = (stillBobSpeed * level.time) & FINEMASK;

		*y += FixedMul(stillBobRange, finesine[(int)(stillBobAngle & (FINEANGLES / 2 - 1))]);
	}

	float viewSwaySpeed = 0.0f;
	fixed_t motionSwaySpeed = 0;
	fixed_t jumpSwaySpeed = 0;

	// [AK] Choose between the client's settings or the weapon's properties.
	if (cl_usecustomsway)
	{
		viewSwaySpeed = cl_viewswayspeed;
		motionSwaySpeed = FLOAT2FIXED(cl_motionswayspeed);
		jumpSwaySpeed = FLOAT2FIXED(cl_jumpswayspeed);
	}
	else
	{
		viewSwaySpeed = FIXED2FLOAT(weapon->ViewSwaySpeed);
		motionSwaySpeed = weapon->MotionSwaySpeed;
		jumpSwaySpeed = weapon->JumpSwaySpeed;
	}

	// [AK] Sway the weapon if any of the multipliers are non-zero values.
	if ((viewSwaySpeed != 0.0f) || (motionSwaySpeed != 0) || (jumpSwaySpeed != 0))
	{
		static fixed_t swaypos[2];
		static int lastSwayTime = 0;
		const int swayStyle = cl_usecustomsway ? cl_swaystyle : weapon->SwayStyle;

		// [AK] Don't reposition the sprite while the ticker is paused or while the server is lagging.
		if ((lastSwayTime != level.time) && (paused == false) && (P_CheckTickerPaused() == false) && (CLIENT_GetServerLagging() == false))
		{
			fixed_t nswaypos[2] = {0, 0};

			if (viewSwaySpeed != 0.0f)
			{
				nswaypos[0] += FLOAT2FIXED(FIXED2FLOAT(player->mo->AngleDelta) * viewSwaySpeed / 256.0f);
				nswaypos[1] += FLOAT2FIXED(FIXED2FLOAT(player->mo->PitchDelta) * viewSwaySpeed / 256.0f);
			}

			// [AK] Add additional vertical sway when the player jumps in the air. Don't do this after
			// the player's speed in the z-axis is greater than their jump speed.
			if ((jumpSwaySpeed != 0) && (player->onground == false) && (player->jumpTics != 0) && (abs(player->mo->velz) <= player->mo->CalcJumpVelz()))
			{
				nswaypos[1] += FixedMul(player->mo->velz, jumpSwaySpeed);
			}
			// [AK] Add additional vertical sway when the player moves and/or crouches up or down.
			else if (motionSwaySpeed != 0)
			{
				const fixed_t zDiffMax = FixedMul(10 << FRACBITS, motionSwaySpeed);
				const fixed_t zDiff = clamp<fixed_t>(player->mo->z - player->mo->PrevZ, -zDiffMax, zDiffMax);

				nswaypos[1] += FixedMul(zDiff + player->crouchviewdelta / 2, motionSwaySpeed);
			}

			for (int i = 0; i <= 1; i++)
			{
				if (abs(nswaypos[i] - swaypos[i]) <= 256)
				{
					swaypos[i] = nswaypos[i];
				}
				else
				{
					fixed_t zoom = MAX<fixed_t>(256, abs(swaypos[i] - nswaypos[i]) / 10);
					swaypos[i] += zoom * (swaypos[i] > nswaypos[i] ? -1 : 1);
				}
			}

			lastSwayTime = level.time;
		}

		*x += swaypos[0];

		switch (swayStyle)
		{
		case WEAPON_SWAY_NORMAL:
			*y += swaypos[1];
			break;

		case WEAPON_SWAY_DOWNONLY:
			*y += MAX<fixed_t>(0, swaypos[1]);
			break;

		case WEAPON_SWAY_UPONLY:
			*y += MIN<fixed_t>(0, swaypos[1]);
			break;
		}
	}

	int viewpitchstyle = cl_usecustompitch ? cl_viewpitchstyle : weapon->ViewPitchStyle;
	float viewpitchoffset = cl_usecustompitch ? cl_viewpitchoffset : FIXED2FLOAT(weapon->ViewPitchOffset);

	// [AK] Offset the weapon based on the player's pitch if the multiplier is a non-zero value.
	if (viewpitchoffset != 0.0f)
	{
		fixed_t halfmin = FIXED_MIN >> 1;
		fixed_t value = 0;

		switch (viewpitchstyle)
		{
		case WEAPON_PITCH_FULL:
			value = FixedDiv(halfmin + player->mo->pitch, FIXED_MIN);
			break;

		case WEAPON_PITCH_UPONLY:
			value = FixedDiv(MIN<fixed_t>(0, player->mo->pitch), halfmin);
			break;

		case WEAPON_PITCH_DOWNONLY:
			value = -FixedDiv(MAX<fixed_t>(0, player->mo->pitch), halfmin);
			break;

		case WEAPON_PITCH_DOWNANDUP:
			value = -FixedDiv(abs(player->mo->pitch), halfmin);
			break;

		case WEAPON_PITCH_CENTERED: // [JM] Dark Forces style, where facing forward is no offset.
			value = -FixedDiv(player->mo->pitch, halfmin);
			break;
		}

		*y -= FixedMul(value, FLOAT2FIXED(viewpitchoffset)) + MIN<fixed_t>(0, FLOAT2FIXED(viewpitchoffset));
	}
}

//============================================================================
//
// PROC A_WeaponReady
//
// Readies a weapon for firing or bobbing with its three ancillary functions,
// DoReadyWeaponToSwitch(), DoReadyWeaponToFire() and DoReadyWeaponToBob().
// [XA] Added DoReadyWeaponToReload() and DoReadyWeaponToZoom()
//
//============================================================================

void DoReadyWeaponToSwitch (AActor *self, bool switchable)
{
	// Prepare for switching action.
	player_t *player;
	if (self && (player = self->player))
	{
		if (switchable)
		{
			player->WeaponState |= WF_WEAPONSWITCHOK | WF_REFIRESWITCHOK;
		}
		else
		{
			// WF_WEAPONSWITCHOK is automatically cleared every tic by P_SetPsprite().
			player->WeaponState &= ~WF_REFIRESWITCHOK;
		}
	}
}

void DoReadyWeaponDisableSwitch (AActor *self, INTBOOL disable)
{
	// Discard all switch attempts?
	player_t *player;
	if (self && (player = self->player))
	{
		if (disable)
		{
			player->WeaponState |= WF_DISABLESWITCH;
			player->WeaponState &= ~WF_REFIRESWITCHOK;
		}
		else
		{
			player->WeaponState &= ~WF_DISABLESWITCH;
		}
	}
}

void DoReadyWeaponToFire (AActor *self, bool prim, bool alt)
{
	player_t *player;
	AWeapon *weapon;

	if (!self || !(player = self->player) || !(weapon = player->ReadyWeapon))
	{
		return;
	}

	// Change player from attack state
	if (self->InStateSequence(self->state, self->MissileState) ||
		self->InStateSequence(self->state, self->MeleeState))
	{
		static_cast<APlayerPawn *>(self)->PlayIdle ();
	}

	// Play ready sound, if any.
	if (weapon->ReadySound && player->psprites[ps_weapon].state == weapon->FindState(NAME_Ready))
	{
		if (!(weapon->WeaponFlags & WIF_READYSNDHALF) || pr_wpnreadysnd() < 128)
		{
			S_Sound (self, CHAN_WEAPON, weapon->ReadySound, 1, ATTN_NORM);

			// [BC] If we're the server, tell other clients to play the sound.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SoundActor( self, CHAN_WEAPON, S_GetName( weapon->ReadySound ), 1, ATTN_NORM, ULONG( player - players ), SVCF_SKIPTHISCLIENT );
		}
	}

	// Prepare for firing action.
	player->WeaponState |= ((prim ? WF_WEAPONREADY : 0) | (alt ? WF_WEAPONREADYALT : 0));
	return;
}

void DoReadyWeaponToBob (AActor *self)
{
	if (self && self->player && self->player->ReadyWeapon)
	{
		// Prepare for bobbing action.
		self->player->WeaponState |= WF_WEAPONBOBBING;
		self->player->psprites[ps_weapon].sx = 0;
		self->player->psprites[ps_weapon].sy = WEAPONTOP;
	}
}

void DoReadyWeaponToReload (AActor *self)
{
	// Prepare for reload action.
	player_t *player;
	if (self && (player = self->player))
		player->WeaponState |= WF_WEAPONRELOADOK;
	return;
}

void DoReadyWeaponToZoom (AActor *self)
{
	// Prepare for reload action.
	player_t *player;
	if (self && (player = self->player))
		player->WeaponState |= WF_WEAPONZOOMOK;
	return;
}

// This function replaces calls to A_WeaponReady in other codepointers.
void DoReadyWeapon(AActor *self)
{
	DoReadyWeaponToBob(self);
	DoReadyWeaponToFire(self);
	DoReadyWeaponToSwitch(self);
	DoReadyWeaponToReload(self);
	DoReadyWeaponToZoom(self);
}

enum EWRF_Options
{
	WRF_NoBob = 1,
	WRF_NoSwitch = 2,
	WRF_NoPrimary = 4,
	WRF_NoSecondary = 8,
	WRF_NoFire = WRF_NoPrimary + WRF_NoSecondary,
	WRF_AllowReload = 16,
	WRF_AllowZoom = 32,
	WRF_DisableSwitch = 64,
};

DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_WeaponReady)
{
	ACTION_PARAM_START(1);
	ACTION_PARAM_INT(paramflags, 0);

													DoReadyWeaponToSwitch(self, !(paramflags & WRF_NoSwitch));
	if ((paramflags & WRF_NoFire) != WRF_NoFire)	DoReadyWeaponToFire(self, !(paramflags & WRF_NoPrimary), !(paramflags & WRF_NoSecondary));
	if (!(paramflags & WRF_NoBob))					DoReadyWeaponToBob(self);
	if ((paramflags & WRF_AllowReload))				DoReadyWeaponToReload(self);
	if ((paramflags & WRF_AllowZoom))				DoReadyWeaponToZoom(self);

	DoReadyWeaponDisableSwitch(self, paramflags & WRF_DisableSwitch);
}

//---------------------------------------------------------------------------
//
// PROC P_CheckWeaponFire
//
// The player can fire the weapon.
// [RH] This was in A_WeaponReady before, but that only works well when the
// weapon's ready frames have a one tic delay.
//
//---------------------------------------------------------------------------

void P_CheckWeaponFire (player_t *player)
{
	AWeapon *weapon = player->ReadyWeapon;

	if (weapon == NULL)
		return;

	// Check for fire. Some weapons do not auto fire.
	if ((player->WeaponState & WF_WEAPONREADY) && (player->cmd.ucmd.buttons & BT_ATTACK))
	{
		if (!player->attackdown || !(weapon->WeaponFlags & WIF_NOAUTOFIRE))
		{
			player->attackdown = true;
			P_FireWeapon (player, NULL);
			return;
		}
	}
	else if ((player->WeaponState & WF_WEAPONREADYALT) && (player->cmd.ucmd.buttons & BT_ALTATTACK))
	{
		if (!player->attackdown || !(weapon->WeaponFlags & WIF_NOAUTOFIRE))
		{
			player->attackdown = true;
			P_FireWeaponAlt (player, NULL);
			return;
		}
	}
	else
	{
		player->attackdown = false;
	}
}

//---------------------------------------------------------------------------
//
// PROC P_CheckWeaponSwitch
//
// The player can change to another weapon at this time.
// [GZ] This was cut from P_CheckWeaponFire.
//
//---------------------------------------------------------------------------

void P_CheckWeaponSwitch (player_t *player)
{
	if (player == NULL)
	{
		return;
	}
	if ((player->WeaponState & WF_DISABLESWITCH) || // Weapon changing has been disabled.
		( player->morphTics != 0 && // Morphed classes cannot change weapons.
		!( player->mo && (player->mo->PlayerFlags & PPF_NOMORPHLIMITATIONS) ) )) // [geNia] unless +NOMORPHLIMITATIONS is used
	{ // ...so throw away any pending weapon requests.
		player->PendingWeapon = WP_NOCHANGE;
	}

	// Put the weapon away if the player has a pending weapon or has died, and
	// we're at a place in the state sequence where dropping the weapon is okay.
	if ((player->PendingWeapon != WP_NOCHANGE || player->health <= 0) &&
		player->WeaponState & WF_WEAPONSWITCHOK)
	{
		P_DropWeapon(player);
	}
}

//---------------------------------------------------------------------------
//
// PROC P_CheckWeaponReload
//
// The player can reload the weapon.
//
//---------------------------------------------------------------------------

void P_CheckWeaponReload (player_t *player)
{
	AWeapon *weapon = player->ReadyWeapon;

	if (weapon == NULL)
		return;

	// Check for reload.
	if ((player->WeaponState & WF_WEAPONRELOADOK) && (player->cmd.ucmd.buttons & BT_RELOAD))
	{
		P_ReloadWeapon (player, NULL);
	}
}

//---------------------------------------------------------------------------
//
// PROC P_CheckWeaponZoom
//
// The player can use the weapon's zoom function.
//
//---------------------------------------------------------------------------

void P_CheckWeaponZoom (player_t *player)
{
	AWeapon *weapon = player->ReadyWeapon;

	if (weapon == NULL)
		return;

	// Check for zoom.
	if ((player->WeaponState & WF_WEAPONZOOMOK) && (player->cmd.ucmd.buttons & BT_ZOOM))
	{
		P_ZoomWeapon (player, NULL);
	}
}

//---------------------------------------------------------------------------
//
// PROC A_ReFire
//
// The player can re-fire the weapon without lowering it entirely.
//
//---------------------------------------------------------------------------

DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_ReFire)
{
	ACTION_PARAM_START(1)
	ACTION_PARAM_STATE(state, 0);

	A_ReFire(self, state);
}

void A_ReFire(AActor *self, FState *state)
{
	player_t *player = self->player;
	bool pending;

	if (NULL == player)
	{
		return;
	}
	pending = player->PendingWeapon != WP_NOCHANGE && (player->WeaponState & WF_REFIRESWITCHOK);
	if ((player->cmd.ucmd.buttons & BT_ATTACK)
		&& !player->ReadyWeapon->bAltFire && !pending && player->health > 0)
	{
		player->refire++;
		P_FireWeapon (player, state);
	}
	else if ((player->cmd.ucmd.buttons & BT_ALTATTACK)
		&& player->ReadyWeapon->bAltFire && !pending && player->health > 0)
	{
		player->refire++;
		P_FireWeaponAlt (player, state);
	}
	else
	{
		player->refire = 0;
		player->ReadyWeapon->CheckAmmo (player->ReadyWeapon->bAltFire
			? AWeapon::AltFire : AWeapon::PrimaryFire, true);
	}
}

DEFINE_ACTION_FUNCTION(AInventory, A_ClearReFire)
{
	player_t *player = self->player;

	if (NULL != player)
	{
		player->refire = 0;
	}
}

//---------------------------------------------------------------------------
//
// PROC A_CheckReload
//
// Present in Doom, but unused. Also present in Strife, and actually used.
// This and what I call A_XBowReFire are actually the same thing in Strife,
// not two separate functions as I have them here.
//
//---------------------------------------------------------------------------

DEFINE_ACTION_FUNCTION(AInventory, A_CheckReload)
{
	if (self->player != NULL)
	{
		self->player->ReadyWeapon->CheckAmmo (
			self->player->ReadyWeapon->bAltFire ? AWeapon::AltFire
			: AWeapon::PrimaryFire, true);
	}
}

//---------------------------------------------------------------------------
//
// PROC A_Lower
//
//---------------------------------------------------------------------------

DEFINE_ACTION_FUNCTION(AInventory, A_Lower)
{
	player_t *player = self->player;
	pspdef_t *psp;

	if (NULL == player)
	{
		return;
	}
	psp = &player->psprites[ps_weapon];

	// [BC] If we're a spectator, lower weapon completely and do not raise it.
	if ( player->bSpectating )
	{
		psp->sy = WEAPONBOTTOM;
		return;
	}

	// [Binary] Allow morphs to switch weapons if +NOMORPHLIMITATIONS is used.
	if ( ( player->morphTics && !( player->mo && (player->mo->PlayerFlags & PPF_NOMORPHLIMITATIONS) ) ) || player->cheats & CF_INSTANTWEAPSWITCH)
	{
		psp->sy = WEAPONBOTTOM;
	}
	else
	{
		psp->sy += LOWERSPEED;
	}
	if (psp->sy < WEAPONBOTTOM)
	{ // Not lowered all the way yet
		return;
	}
	if (player->playerstate == PST_DEAD)
	{ // Player is dead, so don't bring up a pending weapon
		psp->sy = WEAPONBOTTOM;
	
		// Player is dead, so keep the weapon off screen
		P_SetPsprite (player,  ps_weapon, NULL);
		return;
	}
	// [RH] Clear the flash state. Only needed for Strife.
	P_SetPsprite (player, ps_flash, NULL);
	P_BringUpWeapon (player);
}

//---------------------------------------------------------------------------
//
// PROC A_Raise
//
//---------------------------------------------------------------------------

DEFINE_ACTION_FUNCTION(AInventory, A_Raise)
{
	if (self == NULL)
	{
		return;
	}
	player_t *player = self->player;
	pspdef_t *psp;

	if (NULL == player)
	{
		return;
	}
	// [BB] ZACOMPATF_OLD_WEAPON_SWITCH also restores the original weapon switch cancellation behavior.
	// [CK] Changed to now be separate from ZACOMPATF_OLD_WEAPON_SWITCH
	if (player->PendingWeapon != WP_NOCHANGE && !( zacompatflags & ZACOMPATF_FULL_WEAPON_LOWER ))
	{
		P_DropWeapon(player);
		return;
	}
	psp = &player->psprites[ps_weapon];
	psp->sy -= RAISESPEED;
	if (psp->sy > WEAPONTOP)
	{ // Not raised all the way yet
		return;
	}
	psp->sy = WEAPONTOP;
	if (player->ReadyWeapon != NULL)
	{
		P_SetPsprite (player, ps_weapon, player->ReadyWeapon->GetReadyState());
	}
	else
	{
		player->psprites[ps_weapon].state = NULL;
	}

	// [BC] If this player has respawn invulnerability, disable it if they're done raising
	// a weapon that isn't the pistol or their fist.
	if ((player->mo) && (NETWORK_InClientMode() == false))
	{
		APowerRespawnInvulnerable *invul = static_cast<APowerRespawnInvulnerable *>(player->mo->FindInventory (RUNTIME_CLASS(APowerRespawnInvulnerable)));

		if ((invul) && (player->ReadyWeapon) && ((player->ReadyWeapon->WeaponFlags & WIF_ALLOW_WITH_RESPAWN_INVUL) == false))
		{
			invul->Destroy();

			// If we're the server, tell clients to take this player's powerup away.
			if (NETWORK_GetState() == NETSTATE_SERVER)
				SERVERCOMMANDS_TakeInventory (static_cast<unsigned>(player - players), RUNTIME_CLASS(APowerRespawnInvulnerable), 0);
		}
	}
}




//
// A_GunFlash
//
enum GF_Flags
{
	GFF_NOEXTCHANGE = 1,
};

DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_GunFlash)
{
	ACTION_PARAM_START(2)
	ACTION_PARAM_STATE(flash, 0);
	ACTION_PARAM_INT(Flags, 1);

	// [BB] Zandronum needs A_GunFlash in a_doomweaps, so I moved the code into a function.
	A_GunFlash(self, flash, Flags);
}

void A_GunFlash(AActor *self, FState *flash, const int Flags)
{
	player_t *player = self->player;

	if (NULL == player)
	{
		return;
	}
	if(!(Flags & GFF_NOEXTCHANGE))
	{
		// [BC] Since the player can be dead at this point as a result of shooting a player with
		// the reflection rune, we need to make sure the player is alive before playing the
		// attacking animation.
		if ( player->mo->health > 0 )
		{
			// [BB] If we're the server, tell clients to update this player's state.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SetPlayerState( ULONG( player - players ), STATE_PLAYER_ATTACK2, ULONG( player - players ), SVCF_SKIPTHISCLIENT );

			// [BB] Clients only do this for "their" player.
			if ( NETWORK_IsConsolePlayerOrNotInClientMode( player ) )
				player->mo->PlayAttacking2 ();
		}
	}

	// [BB] In a crash log, client_GiveInventory was calling this with ReadyWeapon == NULL when giving some CustomInventory.
	if (flash == NULL && player->ReadyWeapon)
	{
		if (player->ReadyWeapon->bAltFire) flash = player->ReadyWeapon->FindState(NAME_AltFlash);
		if (flash == NULL) flash = player->ReadyWeapon->FindState(NAME_Flash);
	}
	P_SetPsprite (player, ps_flash, flash);
}

//---------------------------------------------------------------------------
//
// [rc4l] The A_Overlay family, adapting GZDoom's DECORATE overlay functions and their
// PSPF_/WOF_ flag semantics to this engine's psprite model (GZDoom implements them on its
// DPSprite/VM rewrite; this is a behavioural reimplementation, not a line port, so no single
// upstream SHA pins it).
// [overlay] Resolve a layer argument of 0 to the layer whose state is currently executing,
// so overlays can act on themselves.
//
//---------------------------------------------------------------------------

static int P_ResolveOverlayLayer(int layer)
{
	return layer != 0 ? layer : P_GetCurrentPSpriteLayer();
}

//
// A_Overlay(int layer, state st, bool nooverride = false)
// Place (or replace) a psprite on an arbitrary layer.
//
DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_Overlay)
{
	ACTION_PARAM_START(3);
	ACTION_PARAM_INT(layer, 0);
	ACTION_PARAM_STATE(st, 1);
	ACTION_PARAM_BOOL(nooverride, 2);

	player_t *player = self->player;
	if (player == NULL)
		return;

	layer = P_ResolveOverlayLayer(layer);
	if (layer == 0)
		return; // layer 0 is not a valid target

	if (nooverride)
	{
		pspdef_t *existing = player->psprites.Find(layer);
		if (existing != NULL && existing->state != NULL)
			return;
	}

	P_SetPsprite(player, layer, st);
}

//
// A_ClearOverlays(int start = 0, int stop = 0, bool safety = true)
// Remove overlay layers in [start, stop] (or all when both are 0).
//
DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_ClearOverlays)
{
	ACTION_PARAM_START(3);
	ACTION_PARAM_INT(start, 0);
	ACTION_PARAM_INT(stop, 1);
	ACTION_PARAM_BOOL(safety, 2);

	player_t *player = self->player;
	if (player == NULL)
		return;

	player->psprites.ClearRange(start, stop, safety);
}

//
// A_OverlayFlags(int layer, int flags, bool set)
// Set or clear PSPF_* flags on a layer.
//
DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_OverlayFlags)
{
	ACTION_PARAM_START(3);
	ACTION_PARAM_INT(layer, 0);
	ACTION_PARAM_INT(flags, 1);
	ACTION_PARAM_BOOL(set, 2);

	player_t *player = self->player;
	if (player == NULL)
		return;

	pspdef_t *psp = player->psprites.Find(P_ResolveOverlayLayer(layer));
	if (psp == NULL)
		return;

	psp->Flags = ComputeOverlayFlags((unsigned int)psp->Flags, (unsigned int)flags, set);
}

//
// A_OverlayOffset(int layer = 1, float wx = 0, float wy = 32, int flags = 0)
// Move a layer, honoring WOF_KEEPX / WOF_KEEPY / WOF_ADD.
//
DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_OverlayOffset)
{
	ACTION_PARAM_START(4);
	ACTION_PARAM_INT(layer, 0);
	ACTION_PARAM_FIXED(wx, 1);
	ACTION_PARAM_FIXED(wy, 2);
	ACTION_PARAM_INT(flags, 3);

	player_t *player = self->player;
	if (player == NULL)
		return;

	pspdef_t *psp = player->psprites.Find(P_ResolveOverlayLayer(layer));
	if (psp == NULL)
		return;

	bool keepx = (flags & WOF_KEEPX) != 0;
	bool keepy = (flags & WOF_KEEPY) != 0;
	bool add = (flags & WOF_ADD) != 0;

	// [overlay] WOF_INTERPOLATE forces smoothing for this tic; a plain move (no ADD and no
	// INTERPOLATE) snaps instead, matching GZDoom's backwards-compatible behaviour. oldx/oldy
	// are owned by P_MovePsprites, which snapshots them at the start of every tic.
	if (flags & WOF_INTERPOLATE)
		psp->bInterpolate = true;
	else if ((flags & WOF_ADD) == 0)
		psp->bInterpolate = false;

	// [overlay] The pure helper works on raw 48.16 fixed values (fixed_t is now zx::Fixed).
	psp->sx = fixed_t::FromRaw(ComputeOverlayAxis(psp->sx.Raw(), wx.Raw(), keepx, add));
	psp->sy = fixed_t::FromRaw(ComputeOverlayAxis(psp->sy.Raw(), wy.Raw(), keepy, add));
}

//
// A_OverlayAlpha(int layer, float alpha)
// Set a layer's alpha. Only visible when the layer has PSPF_ALPHA/PSPF_FORCEALPHA set.
//
DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_OverlayAlpha)
{
	ACTION_PARAM_START(2);
	ACTION_PARAM_INT(layer, 0);
	ACTION_PARAM_FIXED(alpha, 1);

	player_t *player = self->player;
	if (player == NULL)
		return;

	pspdef_t *psp = player->psprites.Find(P_ResolveOverlayLayer(layer));
	if (psp == NULL)
		return;

	psp->alpha = clamp<fixed_t>(alpha, 0, FRACUNIT);
}

//
// A_OverlayRenderStyle(int layer, int style)
// Set a layer's render style. Only used when PSPF_RENDERSTYLE/PSPF_FORCESTYLE is set.
//
DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_OverlayRenderStyle)
{
	ACTION_PARAM_START(2);
	ACTION_PARAM_INT(layer, 0);
	ACTION_PARAM_INT(style, 1);

	player_t *player = self->player;
	if (player == NULL)
		return;

	if (style < 0 || style >= STYLE_Count)
		return;

	pspdef_t *psp = player->psprites.Find(P_ResolveOverlayLayer(layer));
	if (psp == NULL)
		return;

	psp->RenderStyle = LegacyRenderStyles[style];
}

//
// A_OverlayScale(int layer, float scalex = 1, float scaley = 0, int flags = 0)
// Resize a layer, honoring WOF_KEEPX / WOF_KEEPY / WOF_ADD. A scaley of 0 copies scalex.
//
DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_OverlayScale)
{
	ACTION_PARAM_START(4);
	ACTION_PARAM_INT(layer, 0);
	ACTION_PARAM_FIXED(scalex, 1);
	ACTION_PARAM_FIXED(scaley, 2);
	ACTION_PARAM_INT(flags, 3);

	player_t *player = self->player;
	if (player == NULL)
		return;

	pspdef_t *psp = player->psprites.Find(P_ResolveOverlayLayer(layer));
	if (psp == NULL)
		return;

	// [overlay] Raw 48.16 fixed values through the pure helpers (fixed_t is now zx::Fixed).
	scaley = fixed_t::FromRaw(ComputeOverlaySquareScaleY(scalex.Raw(), scaley.Raw()));

	bool keepx = (flags & WOF_KEEPX) != 0;
	bool keepy = (flags & WOF_KEEPY) != 0;
	bool add = (flags & WOF_ADD) != 0;

	psp->scalex = fixed_t::FromRaw(ComputeOverlayAxis(psp->scalex.Raw(), scalex.Raw(), keepx, add));
	psp->scaley = fixed_t::FromRaw(ComputeOverlayAxis(psp->scaley.Raw(), scaley.Raw(), keepy, add));
}



//
// WEAPON ATTACKS
//

//
// P_BulletSlope
// Sets a slope so a near miss is at aproximately
// the height of the intended target
//

angle_t P_BulletSlope (AActor *mo, AActor **pLineTarget)
{
	static const int angdiff[15] = {
		AUTOAIM_MINANGLE * -1, AUTOAIM_MINANGLE * 1, AUTOAIM_MINANGLE * -2, AUTOAIM_MINANGLE * 2,
		AUTOAIM_MINANGLE * -3, AUTOAIM_MINANGLE * 3, AUTOAIM_MINANGLE * -4, AUTOAIM_MINANGLE * 4,
		AUTOAIM_MINANGLE * -5, AUTOAIM_MINANGLE * 5, AUTOAIM_MINANGLE * -6, AUTOAIM_MINANGLE * 6,
		-( 1<<26 ), 1<<26, 0 }; // [CK] New angles
	int i;
	angle_t an;
	angle_t pitch;
	AActor *linetarget;
	int endIndex = zacompatflags & ZACOMPATF_AUTOAIM ? 12 : 0; // [CK/TP] Our ending index depends on compatflags.

	// [Spleen]
	UNLAGGED_Reconcile( mo );
	UNLAGGED_AddReconciliationBlocker( );

	// see which target is to be aimed at
	i = 14; // [TP/CK] Now 14
	do
	{
		an = mo->angle + angdiff[i];
		pitch = (angle_t)(P_AimLineAttack (mo, an, 16*64*FRACUNIT, &linetarget));

		if (mo->player != NULL &&
			level.IsFreelookAllowed() &&
			mo->player->userinfo.GetAimDist() <= ANGLE_1/2)
		{
			break;
		}
	} while (linetarget == NULL && --i >= endIndex); // [TP] 0 changed to endIndex
	if (pLineTarget != NULL)
	{
		*pLineTarget = linetarget;
	}

	// [Spleen]
	UNLAGGED_RemoveReconciliationBlocker( );
	UNLAGGED_Restore( mo );

	return pitch;
}


//
// P_GunShot
//
void P_GunShot (AActor *mo, bool accurate, const PClass *pufftype, angle_t pitch)
{
	angle_t 	angle;
	int 		damage;
		
	damage = 5*(pr_gunshot()%3+1);
	angle = mo->angle;

	if (!accurate)
	{
		angle += pr_gunshot.Random2 () << 18;
	}

	P_LineAttack (mo, angle, PLAYERMISSILERANGE, pitch, damage, NAME_Hitscan, pufftype);
}

DEFINE_ACTION_FUNCTION(AInventory, A_Light0)
{
	if (self->player != NULL)
	{
		self->player->extralight = 0;
	}
}

DEFINE_ACTION_FUNCTION(AInventory, A_Light1)
{
	if (self->player != NULL)
	{
		self->player->extralight = 1;
	}
}

DEFINE_ACTION_FUNCTION(AInventory, A_Light2)
{
	if (self->player != NULL)
	{
		self->player->extralight = 2;
	}
}

DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_Light)
{
	ACTION_PARAM_START(1);
	ACTION_PARAM_INT(light, 0);

	if (self->player != NULL)
	{
		self->player->extralight = clamp<int>(light, -20, 20);
	}
}

//------------------------------------------------------------------------
//
// PROC P_SetupPsprites
//
// Called at start of level for each player
//
//------------------------------------------------------------------------

void P_SetupPsprites(player_t *player, bool startweaponup)
{
	// [overlay] Remove all psprites: drop overlays and deactivate the reserved layers.
	player->psprites.ResetToReserved();
	// Spawn the ready weapon
	player->PendingWeapon = !startweaponup ? player->ReadyWeapon : WP_NOCHANGE;
	P_BringUpWeapon (player);
}

//------------------------------------------------------------------------
//
// PROC P_MovePsprites
//
// Called every tic by player thinking routine
//
//------------------------------------------------------------------------

void P_MovePsprites (player_t *player)
{
	pspdef_t *psp;
	FState *state;

	// [RH] If you don't have a weapon, then the psprites should be NULL.
	if (player->ReadyWeapon == NULL && (player->health > 0 || player->mo->DamageType != NAME_Fire))
	{
		P_SetPsprite (player, ps_weapon, NULL);
		P_SetPsprite (player, ps_flash, NULL);
		if (player->PendingWeapon != WP_NOCHANGE)
		{
			P_BringUpWeapon (player);
		}
	}
	else
	{
		// [overlay] Snapshot the layer ids up front: advancing a state runs its action,
		// which may add or remove layers, so a live iteration could be invalidated.
		TArray<int> ids;
		for (unsigned int n = 0; n < player->psprites.Size(); n++)
			ids.Push(player->psprites.Element(n).layer);

		for (unsigned int n = 0; n < ids.Size(); n++)
		{
			psp = player->psprites.Find(ids[n]);
			if (psp == NULL)
				continue; // this layer was removed by an earlier action this tick

			// [overlay] Snapshot this tic's starting offset so the renderer can interpolate
			// towards whatever the state actions set below. PSPF_INTERPOLATE opts in; an
			// A_OverlayOffset without ADD/INTERPOLATE turns it back off for this tic.
			psp->oldx = psp->sx;
			psp->oldy = psp->sy;
			psp->bInterpolate = (psp->Flags & PSPF_INTERPOLATE) != 0;

			if ((state = psp->state) != NULL && psp->processPending) // a null state means not active
			{
				// drop tic count and possibly change state
				if (psp->tics != -1)	// a -1 tic count never changes
				{
					psp->tics--;

					// [BC] Apply double firing speed.
					// [overlay] Reserved layers always; overlays only with PSPF_POWDOUBLE.
					if ( psp->tics && (player->cheats & CF_DOUBLEFIRINGSPEED) &&
						 (P_IsReservedPSpriteLayer(ids[n]) || (psp->Flags & PSPF_POWDOUBLE)))
						psp->tics--;

					if(!psp->tics)
					{
						P_SetPsprite (player, ids[n], psp->state->GetNextState());
					}
				}
			}
		}
		player->psprites[ps_flash].sx = player->psprites[ps_weapon].sx;
		player->psprites[ps_flash].sy = player->psprites[ps_weapon].sy;
		P_CheckWeaponSwitch (player);
		if (player->WeaponState & (WF_WEAPONREADY | WF_WEAPONREADYALT))
		{
			P_CheckWeaponFire (player);
		}
		if (player->WeaponState & WF_WEAPONRELOADOK)
		{
			P_CheckWeaponReload (player);
		}
		if (player->WeaponState & WF_WEAPONZOOMOK)
		{
			P_CheckWeaponZoom (player);
		}
	}
}

FArchive &operator<< (FArchive &arc, pspdef_t &def)
{
	arc << def.state << def.tics << def.sx << def.sy
		<< def.sprite << def.frame
		// [overlay] Per-layer overlay state. oldx/oldy are transient (rebuilt each tic).
		<< def.layer << def.Flags << def.alpha << def.RenderStyle.AsDWORD << def.scalex << def.scaley;
	return arc;
}

// [overlay] Serialize the whole layer set; the count varies because overlays are dynamic.
FArchive &operator<< (FArchive &arc, FPSpriteLayers &layers)
{
	if (arc.IsStoring())
	{
		DWORD count = layers.list.Size();
		arc << count;
		for (unsigned int i = 0; i < layers.list.Size(); i++)
			arc << *layers.list[i];
	}
	else
	{
		for (unsigned int i = 0; i < layers.list.Size(); i++)
			delete layers.list[i];
		layers.list.Clear();

		DWORD count = 0;
		arc << count;
		for (DWORD i = 0; i < count; i++)
		{
			pspdef_t *node = new pspdef_t;
			arc << *node;
			layers.list.Push(node);
		}
	}
	return arc;
}
