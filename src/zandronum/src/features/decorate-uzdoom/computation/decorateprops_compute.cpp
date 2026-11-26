// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/decorate-uzdoom/computation/decorateprops_compute.h"

#include "features/mbf21/computation/dehacked_fields_compute.h"

namespace zx { namespace decorateuzdoom {

bool ComputeDecorateGroupStored(GroupKind kind, int value, int *stored)
{
	if (value < (kind == kProjectileGroup ? -1 : 0))
		return false;
	if (value == 0)
	{
		*stored = 0;
		return true;
	}
	switch (kind)
	{
	case kInfightingGroup: *stored = zx::mbf21::ComputeInfightingGroupStored(value); break;
	case kProjectileGroup: *stored = zx::mbf21::ComputeProjectileGroupStored(value); break;
	default: *stored = zx::mbf21::ComputeSplashGroupStored(value); break;
	}
	return true;
}

int ComputeRespawnTics(int skillRespawnTics, int minRespawnTics, int ticRate)
{
	if (minRespawnTics > 0)
		return minRespawnTics;
	if (minRespawnTics < 0)
		return -minRespawnTics * ticRate;
	return skillRespawnTics;
}

int ComputeSelfDamage(int damage, double selfDamageFactor, bool telefrag)
{
	return telefrag ? damage : static_cast<int>(damage * selfDamageFactor);
}

}} // namespace zx::decorateuzdoom
