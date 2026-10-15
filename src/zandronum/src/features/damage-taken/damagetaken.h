// [MobiusU9B] fua_damagetaken: the last strike the console player took, recorded roll by roll and printed on request.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_DAMAGETAKEN_H
#define ZX_DAMAGETAKEN_H

#include "doomtype.h"   // ULONG
#include "name.h"       // FName

class AActor;
class PClass;
class player_t;

namespace zx { namespace damagetaken {

// [MobiusU9B] What one roll cost the player, filled in by P_DamageMobj as it applies armor and health.
struct HitAmounts
{
	int damage;
	int armorAbsorbed;
	int healthLost;
};

// [MobiusU9B] One roll as the server sends it to the player who took it, and as it is recorded offline.
struct Report
{
	int mapTime;
	int damage;
	int armorAbsorbed;
	int healthLost;
	ULONG attackerPlayer;           // [MobiusU9B] MAXPLAYERS when the attacker is not a player.
	const PClass *attackerClass;
	const PClass *weaponClass;
	const char *damageType;
	bool lethal;
};

// [MobiusU9B] Called offline and on the server once a player's armor and health reflect the roll; a client ignores it.
void OnPlayerHit(player_t *player, AActor *source, AActor *inflictor, FName mod, const HitAmounts &hit);

// [MobiusU9B] Called by the client's FuaDamageTaken handler for a roll the server says the console player took.
void OnServerReport(const Report &report);

} } // namespace zx::damagetaken

#endif
