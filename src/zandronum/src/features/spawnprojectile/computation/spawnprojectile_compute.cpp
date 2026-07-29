// [rc4l] See spawnprojectile_compute.h for the sign-convention derivation against upstream.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "spawnprojectile_compute.h"

namespace zx
{

namespace
{
// [rc4l] The engine's FixedMul for 16.16 in raw bits. Kept local so this unit stays header-pure
// (m_fixed.h drags in the strong fixed_t type and the engine's basictypes).
inline int64_t FixedMulBits(int64_t a, int64_t b) { return (a * b) >> 16; }
}

bool SpawnProjectileUsesPitch(int flags)
{
	return (flags & (ZX_CMF_ABSOLUTEPITCH | ZX_CMF_OFFSETPITCH)) != 0;
}

uint32_t ComputeSpawnProjectilePitch(int flags, uint32_t pitch, uint32_t missileVelPitch)
{
	if ((flags & ZX_CMF_OFFSETPITCH) == 0)
		return pitch;

	// [rc4l] The inversion. A_CustomMissile added the missile's own velocity pitch, which is what
	// upstream calls the bogus calculation; the corrected form subtracts it. Modular arithmetic on
	// angle_t is deliberate -- angles wrap.
	return (flags & ZX_CMF_BADPITCH) ? (uint32_t)(pitch + missileVelPitch)
	                                 : (uint32_t)(pitch - missileVelPitch);
}

SpawnProjectileVelocity ComputeSpawnProjectileVelocity(int flags, int64_t sinPitch,
                                                       int64_t cosPitch, int64_t missileSpeed)
{
	SpawnProjectileVelocity out;

	const int64_t horizontal = FixedMulBits(cosPitch, missileSpeed);
	out.speedXY = horizontal < 0 ? -horizontal : horizontal;

	const int64_t vertical = FixedMulBits(sinPitch, missileSpeed);
	// [rc4l] The second half of the inversion: the corrected form drives velz with -sin.
	out.velZ = (flags & ZX_CMF_BADPITCH) ? vertical : -vertical;

	return out;
}

} // namespace zx
