// [MobiusU9B] See damagetaken_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/damage-taken/computation/damagetaken_compute.h"

#include <algorithm>

namespace zx { namespace damagetaken {

namespace
{
	// [MobiusU9B] The label every unnamed or "None" damage type gets.
	const char *const kNormalType = "Normal";

	// [MobiusU9B] Right-aligns a number so the roll lines' columns line up for values below 1000.
	std::string Padded(int value)
	{
		std::string text = std::to_string(value);
		while (text.size() < 3)
			text.insert(text.begin(), ' ');
		return text;
	}

	// [MobiusU9B] "attacker" or "attacker (weapon)".
	std::string Source(const DamageTakenRoll &roll)
	{
		return roll.weapon.empty() ? roll.attacker : roll.attacker + " (" + roll.weapon + ")";
	}

	// [MobiusU9B] True when every roll names the same attacker and weapon, so the header can name them once.
	bool OneSource(const DamageTakenStrike &strike)
	{
		const DamageTakenRoll &first = strike.rolls.front();
		return std::all_of(strike.rolls.begin(), strike.rolls.end(), [&first](const DamageTakenRoll &roll) {
			return roll.attacker == first.attacker && roll.weapon == first.weapon;
		});
	}
}

bool ComputeDamageTakenIsRoll(bool hasInflictor, int armorAbsorbed, int healthLost)
{
	return hasInflictor && (armorAbsorbed > 0 || healthLost > 0);
}

void ComputeDamageTakenAddRoll(DamageTakenStrike &strike, const DamageTakenRoll &roll, const std::string &map, int mapTics)
{
	if (strike.rolls.empty() || strike.map != map || strike.mapTics != mapTics)
	{
		strike.rolls.clear();
		strike.map = map;
		strike.mapTics = mapTics;
	}
	strike.rolls.push_back(roll);
}

int ComputeDamageTakenCountedDamage(const DamageTakenRoll &roll)
{
	return roll.telefrag ? roll.armorAbsorbed + roll.healthLost : roll.damage;
}

std::string ComputeDamageTakenTimeText(int tics, int ticRate)
{
	if (tics < 0 || ticRate <= 0)
		return "0:00.0";
	const long long tenths = static_cast<long long>(tics) * 10 / ticRate;
	const long long seconds = tenths / 10;
	const long long secondsInMinute = seconds % 60;
	return std::to_string(seconds / 60) + (secondsInMinute < 10 ? ":0" : ":") + std::to_string(secondsInMinute)
		+ "." + std::to_string(tenths % 10);
}

std::string ComputeDamageTakenAttackerLabel(bool hasAttacker, bool isSelf, const std::string &playerName, const std::string &actorName)
{
	if (!hasAttacker)
		return "unknown";
	if (isSelf)
		return "yourself";
	if (!playerName.empty())
		return playerName;
	return actorName.empty() ? "unknown" : actorName;
}

DamageTakenWeaponSource ComputeDamageTakenWeaponSource(bool inflictorIsAttacker, bool inflictorIsProjectile, bool inflictorIsShootable, bool attackerHasWeapon)
{
	if (!inflictorIsAttacker && (inflictorIsProjectile || inflictorIsShootable))
		return DamageTakenWeaponSource::Inflictor;
	if (attackerHasWeapon)
		return DamageTakenWeaponSource::AttackerWeapon;
	return inflictorIsAttacker ? DamageTakenWeaponSource::None : DamageTakenWeaponSource::Inflictor;
}

std::string ComputeDamageTakenTypeLabel(const std::string &type)
{
	return (type.empty() || type == "None") ? kNormalType : type;
}

std::vector<std::string> ComputeDamageTakenLines(const DamageTakenStrike &strike, int ticRate)
{
	std::vector<std::string> lines;
	if (strike.rolls.empty())
	{
		lines.emplace_back("You haven't been struck yet.");
		return lines;
	}

	long long damage = 0;
	long long armor = 0;
	long long health = 0;
	bool lethal = false;
	for (const DamageTakenRoll &roll : strike.rolls)
	{
		damage += ComputeDamageTakenCountedDamage(roll);
		armor += roll.armorAbsorbed;
		health += roll.healthLost;
		lethal = lethal || roll.lethal;
	}

	const size_t count = strike.rolls.size();
	lines.push_back("Last strike: " + std::to_string(count) + (count == 1 ? " hit, " : " hits, ") + std::to_string(damage)
		+ " damage (" + std::to_string(armor) + " armor, " + std::to_string(health) + " health)" + (lethal ? ", FATAL" : ""));

	const bool oneSource = OneSource(strike);
	const std::string where = "on " + strike.map + " at " + ComputeDamageTakenTimeText(strike.mapTics, ticRate);
	lines.push_back(oneSource ? "  from " + Source(strike.rolls.front()) + " " + where : "  " + where);

	for (size_t i = 0; i < count; ++i)
	{
		const DamageTakenRoll &roll = strike.rolls[i];
		std::string line = "  #" + std::to_string(i + 1) + "  " + (roll.telefrag ? std::string("telefrag") : Padded(roll.damage) + " dmg")
			+ "  armor " + Padded(roll.armorAbsorbed) + "  health " + Padded(roll.healthLost);
		if (!oneSource)
			line += "  " + Source(roll);
		if (roll.type != kNormalType)
			line += "  " + roll.type;
		if (roll.lethal)
			line += "  FATAL";
		lines.push_back(line);
	}
	return lines;
}

} } // namespace zx::damagetaken
