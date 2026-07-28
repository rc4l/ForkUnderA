// [rc4l] MBF21 damage-group predicates (infighting / projectile / splash), extracted pure so they
// are unit-testable off-engine. Semantics match DSDA-Doom -- kraflab's reference implementation and
// author of the MBF21 spec (see prboom2/src/p_inter.c P_InfightingImmune, p_map.c P_ProjectileImmune
// / P_SplashImmune). The engine (p_interaction.cpp / p_map.cpp) reads each actor's group field and
// calls these; no engine headers here so the logic is testable without the game.
// Ported behavior: DSDA-Doom + Zandronum lz/mbf21; MBF21 spec v1.4.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_MBF21_DAMAGE_GROUPS_COMPUTE_H
#define ZX_MBF21_DAMAGE_GROUPS_COMPUTE_H

namespace zx { namespace mbf21 {

// Group sentinels, matching the MBF21 spec + DSDA-Doom's enums.
enum
{
	IG_DEFAULT   = 0,    // infighting: default (no group-based immunity)
	SG_DEFAULT   = 0,    // splash:     default (radius damage always applies)
	PG_DEFAULT   = 0,    // projectile: default (immunity only to the exact same actor type)
	PG_GROUPLESS = -1,   // projectile: no immunity at all, even to your own species
};

// True when `target` must NOT retaliate against `source`: both share the same, non-default
// infighting group. (P_InfightingImmune.)
bool ComputeInfightingImmune(int targetGroup, int sourceGroup);

// True when `target` is immune to `source`'s projectile damage. (P_ProjectileImmune.)
//   sameType  -- target and source are the same actor type.
//   sameActor -- target IS source (a thing is not made vulnerable to itself by grouplessness).
bool ComputeProjectileImmune(int targetGroup, int sourceGroup, bool sameType, bool sameActor);

// True when `target` is immune to `source`'s splash (radius) damage: both share the same,
// non-default splash group. (P_SplashImmune.)
bool ComputeSplashImmune(int targetGroup, int sourceGroup);

}} // namespace zx::mbf21

#endif // ZX_MBF21_DAMAGE_GROUPS_COMPUTE_H
