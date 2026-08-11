// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <stdio.h>

#include "features/addon-catalogue/computation/weaponspick_compute.h"

namespace zx
{

int FastWeaponsMax()
{
	// p_pspr.cpp: `if (self >= 3) self = 2`. The cvar refuses anything higher on its own, so this is
	// the engine's number rather than the panel's.
	return 2;
}

int FastWeaponsValue(int wanted)
{
	if (wanted < 0)
		return 0;
	if (wanted > FastWeaponsMax())
		return FastWeaponsMax();
	return wanted;
}

std::vector<std::pair<std::string, std::string> > FastWeaponsCvars(bool offered, int wanted)
{
	std::vector<std::pair<std::string, std::string> > out;

	if (!offered)
		return out;

	const int value = FastWeaponsValue(wanted);

	char number[8];
	// Zero to two, so this cannot truncate.
	snprintf(number, sizeof(number), "%d", value);

	out.push_back(std::make_pair(std::string("sv_fastweapons"), std::string(number)));

	// See the header: above zero the ammo the maps place stops being enough, so the two are decided
	// together. Only ever turned ON -- writing "false" at the default stop would overrule the packs
	// whose own cfgs set it because they cannot be finished without it.
	if (value > 0)
		out.push_back(std::make_pair(std::string("sv_infiniteammo"), std::string("true")));

	return out;
}

} // namespace zx
