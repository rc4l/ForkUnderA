// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO

// [MGOOOOOO] When A_FireCustomMissile and A_FireProjectile spend the firing weapon's ammo.
#ifndef ZX_FIREPROJECTILE_AMMO_COMPUTE_H
#define ZX_FIREPROJECTILE_AMMO_COMPUTE_H

namespace zx { namespace fireprojectile {

// [MGOOOOOO] A weapon frame spends it (uzdoom@bc206f21a) and so does a CustomInventory item's chain, as on Zandronum, where mods fire their shots from items.
constexpr bool ComputeFireProjectileUsesAmmo( bool useAmmo, bool hasWeapon, bool calledFromWeapon, bool calledFromInventory )
{
	return useAmmo && hasWeapon && ( calledFromWeapon || calledFromInventory );
}

} }

#endif
