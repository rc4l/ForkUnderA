// [rc4l] Implementation of the MBF21 damage-group predicates. See damage_groups_compute.h.
// The three functions are a 1:1 port of DSDA-Doom's P_InfightingImmune / P_ProjectileImmune /
// P_SplashImmune, kept structurally identical so the reference logic is auditable line-for-line.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "damage_groups_compute.h"

namespace zx { namespace mbf21 {

bool ComputeInfightingImmune(int targetGroup, int sourceGroup)
{
	// not default behaviour, and same group
	return targetGroup != IG_DEFAULT &&
	       targetGroup == sourceGroup;
}

bool ComputeProjectileImmune(int targetGroup, int sourceGroup, bool sameType, bool sameActor)
{
	return
		( // PG_GROUPLESS means no immunity, even to your own species
			targetGroup != PG_GROUPLESS ||
			sameActor
		) &&
		(
			( // target type has default behaviour, and things are the same type
				targetGroup == PG_DEFAULT &&
				sameType
			) ||
			( // target type has special behaviour, and things have the same group
				targetGroup != PG_DEFAULT &&
				targetGroup == sourceGroup
			)
		);
}

bool ComputeSplashImmune(int targetGroup, int sourceGroup)
{
	return targetGroup != SG_DEFAULT &&
	       targetGroup == sourceGroup;
}

}} // namespace zx::mbf21
