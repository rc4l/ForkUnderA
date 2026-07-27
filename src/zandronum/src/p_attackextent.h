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

// [MGOOOOOO] True when an actor's attack hitbox exceeds its physical movement box in either
// dimension -- the only case where the widened box can poke through thin geometry (windows, bars,
// narrow pillars) and hit a body that is really behind that geometry. Such a hit needs an extra
// line-of-sight check; a box that is smaller-or-equal in both dimensions cannot bleed, so callers
// use this to skip the check entirely and avoid the cost.
template<typename T>
inline bool AttackHitboxIsEnlarged(T attackRadius, T physicalRadius, T attackHeight, T physicalHeight)
{
	return attackRadius > physicalRadius || attackHeight > physicalHeight;
}

#endif //__P_ATTACKEXTENT_H__
