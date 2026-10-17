// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The game half of the damage tint, read between tics: how much a floor hurts, and whether an actor stands on one unprotected.

#ifndef ZX_TINTSUBJECT_H
#define ZX_TINTSUBJECT_H

#include "doomtype.h"
#include "textures/textures.h"

class AActor;
struct player_t;

namespace zx { namespace gamecopy {

// [rc4l] What the tint needs of an actor: whose it is, whether it is a player's own body, and whether its floor hurts it unprotected and which floor that is.
struct TintSubject
{
	unsigned int id;   // [rc4l] its render copy id, 0 for none
	bool playerBody;
	bool active;
	FTextureID texture;
};

TintSubject TintSubjectOf(AActor *actor);

// [rc4l] How much the floor under an actor hurts it, across sector specials, TERRAIN and 3D floors, and the texture that floor shows.
int FloorDamage(AActor *actor, FTextureID &texOut);
// [rc4l] Whether a radsuit, invulnerability or god mode keeps the floor from hurting the player.
bool DamageNegated(player_t *player);

}} // namespace zx::gamecopy

// [rc4l] How much a sector's special/damage config hurts per cycle (0 = harmless), the complete mirror of P_PlayerInSpecialSector's tables, for the bridge's world.sectors query.
struct sector_t;
int DamageTint_SectorDamage( const sector_t *sec );

#endif // ZX_TINTSUBJECT_H
