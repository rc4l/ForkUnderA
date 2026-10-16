// [MobiusU9B] The last strike the player took, roll by roll, and its console text, kept engine-free so they are unit-tested.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_DAMAGETAKEN_COMPUTE_H
#define ZX_DAMAGETAKEN_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace damagetaken {

// [MobiusU9B] One damage roll that landed: a single pellet, a direct hit or a splash.
struct DamageTakenRoll
{
	int damage;             // [MobiusU9B] After skill, powerups and damage factors, before armor.
	int armorAbsorbed;
	int healthLost;
	bool telefrag;
	bool lethal;
	std::string attacker;   // [MobiusU9B] Already a label, e.g. "Imp", a player name or "yourself".
	std::string weapon;     // [MobiusU9B] Empty when nothing is worth naming.
	std::string type;       // [MobiusU9B] Already a label, e.g. "Normal" or "Fire".
};

// [MobiusU9B] Every roll that landed in one game tic of one map.
struct DamageTakenStrike
{
	std::string map;
	int mapTics;
	std::vector<DamageTakenRoll> rolls;
};

// [MobiusU9B] Which actor names "what hit you".
enum class DamageTakenWeaponSource
{
	None,
	Inflictor,
	AttackerWeapon,
};

// [MobiusU9B] True when the damage came from something that struck the player and actually cost armor or health.
bool ComputeDamageTakenIsRoll(bool hasInflictor, int armorAbsorbed, int healthLost);

// [MobiusU9B] Adds a roll to the strike, first emptying it when the roll belongs to a different map or tic.
void ComputeDamageTakenAddRoll(DamageTakenStrike &strike, const DamageTakenRoll &roll, const std::string &map, int mapTics);

// [MobiusU9B] What a roll adds to the strike's total: its damage, or for a telefrag only the armor and health it removed.
int ComputeDamageTakenCountedDamage(const DamageTakenRoll &roll);

// [MobiusU9B] A map time as "m:ss.t", with negative times and a non-positive tic rate reading 0.
std::string ComputeDamageTakenTimeText(int tics, int ticRate);

// [MobiusU9B] Names the attacker: "yourself", the player's name, the actor's name, or "unknown".
std::string ComputeDamageTakenAttackerLabel(bool hasAttacker, bool isSelf, const std::string &playerName, const std::string &actorName);

// [MobiusU9B] Prefers a projectile or shootable thing, then a player attacker's weapon, then any other inflictor such as a puff.
DamageTakenWeaponSource ComputeDamageTakenWeaponSource(bool inflictorIsAttacker, bool inflictorIsProjectile, bool inflictorIsShootable, bool attackerHasWeapon);

// [MobiusU9B] The damage type's name, with the engine's "None" and an empty name reading "Normal".
std::string ComputeDamageTakenTypeLabel(const std::string &type);

// [MobiusU9B] The console text for the strike, a header then one line per roll, or a single line before any strike.
std::vector<std::string> ComputeDamageTakenLines(const DamageTakenStrike &strike, int ticRate);

} } // namespace zx::damagetaken

#endif
