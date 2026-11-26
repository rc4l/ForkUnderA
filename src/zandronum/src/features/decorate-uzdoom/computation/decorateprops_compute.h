// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_DECORATEPROPS_COMPUTE_H
#define ZX_DECORATEPROPS_COMPUTE_H

namespace zx { namespace decorateuzdoom {

// [Doruk-Aksoy] Which MBF21 group a DECORATE property names.
enum GroupKind
{
	kInfightingGroup,
	kProjectileGroup,
	kSplashGroup,
};

// [Doruk-Aksoy] The stored group for a DECORATE value: 0 is no group as in UZDoom, a positive one is the group our DEHACKED stores for it, -1 groupless for projectiles; false for a value UZDoom refuses.
bool ComputeDecorateGroupStored(GroupKind kind, int value, int *stored);

// [Doruk-Aksoy] How many tics a dead monster waits to respawn: MinRespawnTics when positive, its seconds when negative, else the skill's.
int ComputeRespawnTics(int skillRespawnTics, int minRespawnTics, int ticRate);

// [Doruk-Aksoy] An actor's damage to itself scaled by SelfDamageFactor and truncated, as UZDoom's, telefrag damage left whole.
int ComputeSelfDamage(int damage, double selfDamageFactor, bool telefrag);

}} // namespace zx::decorateuzdoom

#endif // ZX_DECORATEPROPS_COMPUTE_H
