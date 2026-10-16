// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The render copy's players and the actors views are taken from, taken between tics, so a frame sets its view up without a live player.

#ifndef ZX_GAMECOPY_PLAYERS_H
#define ZX_GAMECOPY_PLAYERS_H

#include "features/gamecopy/actors.h"
#include "d_player.h"
#include "p_pspr.h"   // [rc4l] FWeaponBobIn, pspdef_t
#include "r_defs.h"   // [rc4l] visstyle_t
#include "g_shared/a_sharedglobal.h"   // [rc4l] FQuakeJiggers

#include "tarray.h"

#include <vector>

namespace zx { namespace gamecopy {

// [rc4l] One blend a player adds to the screen.
struct BlendLayer { float r, g, b, a; };

// [rc4l] The blends V_AddPlayerBlend adds for a player in order and the alpha it caps them at, none for a player without a body.
struct ScreenBlendLayers
{
	bool applies = false;
	float cap = 0.f;
	std::vector<BlendLayer> layers;
};

// [rc4l] A player as the renderer reads it, its members declared once in fields.h.
struct PlayerRecord
{
#define ZX_PLAYER_FIELD(member) decltype(player_t::member) member;
#define ZX_PLAYER_DERIVED(type, member) type member;
#include "features/gamecopy/fields.h"
#undef ZX_PLAYER_FIELD
#undef ZX_PLAYER_DERIVED
};

// [rc4l] An actor a view is taken from, a player's camera or a camera texture's viewpoint: the quakes shaking it, where the chase camera stands from it, and whether it may pass through floors.
struct ViewSource
{
	int record;
	TArray<FQuakeState> quakes;   // [rc4l] summed by the frame with DEarthquake::StaticQuakeIntensities
	bool chased;
	fixed_t chaseX, chaseY, chaseZ;
	sector_t *chaseSector;
	bool unrestricted;
	visstyle_t weaponVis;   // [rc4l] the weapon's style as the actor's inventory alters it, when it alters one
	bool altersWeapon;
};

// [rc4l] Player n's record as the last fill took it, NULL when n is not in the game.
const PlayerRecord *Player(int n);
// [rc4l] What the last fill found of a record viewed from, NULL when nothing views from it.
const ViewSource *ViewSourceOf(int record);
// [rc4l] The camera R_SetupFrame last set a view up from, which it names here, NULL before any.
const ActorRecord *ViewCamera();
void SetViewCamera(int record);
// [rc4l] The record of whose eyes player n sees through, NULL for none.
const ActorRecord *CameraOf(int n);
// [rc4l] A player's weapon layer, NULL when it has none of that id.
const pspdef_t *Layer(const PlayerRecord &player, int layer);
// [rc4l] Adds a player's screen blend to blend, as V_AddPlayerBlend adds it.
void AddScreenBlend(const ScreenBlendLayers &s, float blend[4]);

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_PLAYERS_H
