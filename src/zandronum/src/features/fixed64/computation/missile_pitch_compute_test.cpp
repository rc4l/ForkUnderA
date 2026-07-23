// [rc4l] Reproduction probe for "plasma spawns at feet when aiming above 0 degrees" (netplay).
// It mirrors, exactly, P_SpawnPlayerMissile's pitch -> vertical-velocity computation:
//     angle_t pitch = (angle_t)( source->pitch );   // fixed_t -> angle_t (the strong Fixed cast)
//     vz = -finesine[ pitch >> ANGLETOFINESHIFT ];
// and sweeps the aim from straight up to straight down, asserting the vertical velocity has the
// physically-correct sign (aim up -> vz > 0). If the widened Fixed->angle_t cast or the table index
// misbehaves for one pitch sign, a shot fired upward gets a downward velz and the projectile drops
// to the floor -- exactly the report.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include <cmath>
#include <cstdint>

#include "features/fixed64/computation/fixed_strong.h"

using zx::Fixed;

namespace
{
constexpr int    FRACBITS        = 16;
constexpr int64_t FRACUNIT       = int64_t(1) << FRACBITS;
constexpr int    FINEANGLES      = 8192;
constexpr int    ANGLETOFINESHIFT = 19;
// [rc4l] BAM angle units, mirroring tables.h: ANGLE_45 = 0x20000000, ANGLE_1 = ANGLE_45/45.
constexpr uint32_t ANGLE_45 = 0x20000000u;
constexpr uint32_t ANGLE_1  = ANGLE_45 / 45;

// [rc4l] finesine sized 5*FINEANGLES/4 so finecosine[i] = finesine[i + FINEANGLES/4]. Values are the
// corrected double(FRACUNIT)*sin fill from r_utility.cpp.
int64_t finesine_at(unsigned idx)
{
	// idx is pitch>>ANGLETOFINESHIFT, range [0, FINEANGLES) for a 32-bit angle.
	return int64_t(double(FRACUNIT) * std::sin(idx * (M_PI * 2 / FINEANGLES)));
}
} // namespace

// [rc4l] The crux: for every elevation from 89 deg up to 89 deg down, the fired projectile's
// vertical velocity must point the way you aim. In ZDoom pitch convention, looking UP is a NEGATIVE
// mo->pitch, and vz = -finesine[(angle_t)pitch >> ANGLETOFINESHIFT] must then be POSITIVE (upward).
TEST(MissilePitch, VerticalVelocityMatchesAimSign)
{
	for (int deg = -89; deg <= 89; ++deg)
	{
		if (deg == 0) continue; // level shot: vz ~ 0, sign undefined

		// mo->pitch is a fixed_t holding a BAM angle: aim up = negative, aim down = positive.
		const Fixed moPitch = Fixed::FromRaw(int64_t(deg) * ANGLE_1);

		// Exactly P_SpawnPlayerMissile: cast the (widened) fixed pitch to angle_t, index finesine.
		const uint32_t pitch = static_cast<uint32_t>(moPitch); // fixed_t -> angle_t
		const int64_t  vz    = -finesine_at(pitch >> ANGLETOFINESHIFT);

		if (deg < 0) // aiming UP
			EXPECT_GT(vz, 0) << "aiming up " << -deg << " deg gave a downward velz (drops to floor)";
		else         // aiming DOWN
			EXPECT_LT(vz, 0) << "aiming down " << deg << " deg gave an upward velz";
	}
}
