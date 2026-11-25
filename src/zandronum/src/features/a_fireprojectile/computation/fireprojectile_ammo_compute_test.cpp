// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO

// [MGOOOOOO] Tests for when a fired projectile spends the weapon's ammo.
#include "gtest/gtest.h"
#include "features/a_fireprojectile/computation/fireprojectile_ammo_compute.h"

using zx::fireprojectile::ComputeFireProjectileUsesAmmo;

// [MGOOOOOO] A weapon frame asking for ammo spends it.
TEST(FireProjectileAmmoCompute, WeaponFrameSpendsAmmo)
{
	EXPECT_TRUE(ComputeFireProjectileUsesAmmo(true, true, true, false));
}

// [MGOOOOOO] A CustomInventory item's chain spends it too, as MM8BDM's classes fire from items on Zandronum.
TEST(FireProjectileAmmoCompute, InventoryChainSpendsAmmo)
{
	EXPECT_TRUE(ComputeFireProjectileUsesAmmo(true, true, false, true));
}

// [MGOOOOOO] The player's own actor states do not, as upstream decided.
TEST(FireProjectileAmmoCompute, ActorStateSpendsNothing)
{
	EXPECT_FALSE(ComputeFireProjectileUsesAmmo(true, true, false, false));
}

// [MGOOOOOO] Nothing is spent without useammo or without a weapon, whoever calls.
TEST(FireProjectileAmmoCompute, NoUseAmmoOrNoWeaponSpendsNothing)
{
	EXPECT_FALSE(ComputeFireProjectileUsesAmmo(false, true, true, true));
	EXPECT_FALSE(ComputeFireProjectileUsesAmmo(true, false, true, true));
}
