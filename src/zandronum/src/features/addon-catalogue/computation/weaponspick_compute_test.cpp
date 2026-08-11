// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/addon-catalogue/computation/weaponspick_compute.h"

using zx::FastWeaponsCvars;
using zx::FastWeaponsMax;
using zx::FastWeaponsValue;

namespace
{

typedef std::vector<std::pair<std::string, std::string> > Cvars;

std::string ValueOf(const Cvars &cvars, const std::string &name)
{
	for (size_t i = 0; i < cvars.size(); ++i)
	{
		if (cvars[i].first == name)
			return cvars[i].second;
	}
	return std::string();
}

} // namespace

TEST(WeaponsPick, TheRangeIsTheEnginesOwn)
{
	// p_pspr.cpp clamps sv_fastweapons at 2 itself. Pinned so the panel and the cvar cannot drift.
	EXPECT_EQ(2, FastWeaponsMax());
}

TEST(WeaponsPick, NothingChosenIsTheSpeedThePackWasTimedAt)
{
	// Any negative, not just -1: no weapon speed below zero is something anybody can have meant.
	EXPECT_EQ(0, FastWeaponsValue(-1));
	EXPECT_EQ(0, FastWeaponsValue(-9));
}

TEST(WeaponsPick, AChoiceAboveTheCeilingLandsOnIt)
{
	EXPECT_EQ(1, FastWeaponsValue(1));
	EXPECT_EQ(2, FastWeaponsValue(2));
	EXPECT_EQ(2, FastWeaponsValue(7));
}

TEST(WeaponsPick, AnEntryThatNeverInvitedTheSettingIsHandedNothing)
{
	// Not even a zero: it would override a cfg that had set the speed itself.
	EXPECT_TRUE(FastWeaponsCvars(false, 2).empty());
	EXPECT_TRUE(FastWeaponsCvars(false, -1).empty());
}

TEST(WeaponsPick, TheDefaultStopSetsTheSpeedAndNothingElse)
{
	const Cvars c = FastWeaponsCvars(true, 0);

	ASSERT_EQ(1u, c.size());
	EXPECT_EQ("sv_fastweapons", c[0].first);
	EXPECT_EQ("0", c[0].second);
}

TEST(WeaponsPick, AnyFasterSpeedBringsInfiniteAmmoWithIt)
{
	// [rc4l] The rule this unit exists for. A weapon firing at two or three times its designed rate
	// empties a backpack in a fraction of the time the pack's ammo placement assumes, so every map
	// balanced on that placement stops being finishable.
	const Cvars fast = FastWeaponsCvars(true, 1);
	const Cvars fastest = FastWeaponsCvars(true, 2);

	EXPECT_EQ("1", ValueOf(fast, "sv_fastweapons"));
	EXPECT_EQ("true", ValueOf(fast, "sv_infiniteammo"));

	EXPECT_EQ("2", ValueOf(fastest, "sv_fastweapons"));
	EXPECT_EQ("true", ValueOf(fastest, "sv_infiniteammo"));
}

TEST(WeaponsPick, TheAmmoIsNeverTurnedBackOff)
{
	// Invasion UAC and Destination Unknown set sv_infiniteammo in their own cfgs because their later
	// maps cannot be finished without it. A control writing "false" at its default stop would
	// silently overrule them, which is a bug the panel could not be seen to have caused.
	const Cvars c = FastWeaponsCvars(true, 0);

	EXPECT_EQ("", ValueOf(c, "sv_infiniteammo"));
}

TEST(WeaponsPick, TheSpeedIsSetBeforeTheAmmo)
{
	const Cvars c = FastWeaponsCvars(true, 2);

	ASSERT_EQ(2u, c.size());
	EXPECT_EQ("sv_fastweapons", c[0].first);
	EXPECT_EQ("sv_infiniteammo", c[1].first);
}
