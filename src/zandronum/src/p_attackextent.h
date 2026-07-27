// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO

#ifndef __P_ATTACKEXTENT_H__
#define __P_ATTACKEXTENT_H__

// [MGOOOOOO] Effective extent used when deciding whether an attack hits an actor: a positive
// per-actor override (PassRadius/PassHeight) wins, otherwise fall back to the actor's physical
// movement extent (radius/height). Templated so it stays dependency-free and unit-testable
// without linking the engine.
template<typename T>
inline T ComputeAttackExtent(T passExtent, T physicalExtent)
{
	return passExtent > 0 ? passExtent : physicalExtent;
}

// [MGOOOOOO] Effective vertical attack extent. Like ComputeAttackExtent, a positive PassHeight
// override wins -- but the override is scaled down to mirror any shrink applied to the physical
// height. Crouching sets an actor's height to defaultHeight * crouchfactor, so passing the current
// and default heights here makes a crouching player's attack box shrink vertically by the same
// factor. It never inflates the override (a taller-than-default actor keeps its full PassHeight),
// and with no override it just returns the physical height (which already reflects the crouch).
template<typename T>
inline T ComputeAttackHeight(T passHeight, T physicalHeight, T defaultHeight)
{
	if (passHeight <= 0)
		return physicalHeight;
	if (defaultHeight > 0 && physicalHeight < defaultHeight)
		return static_cast<T>(static_cast<long long>(passHeight) * physicalHeight / defaultHeight);
	return passHeight;
}

#endif //__P_ATTACKEXTENT_H__
