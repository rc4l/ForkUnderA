// [rc4l] Reproduction harness for the "netplay projectiles/puffs die, angle-dependent" report.
// It models the exact server->client SpawnMissile path -- velocity sent as a full "long" wire field,
// position as an integer "short" field -- and sweeps every fine angle, asserting a projectile's
// velocity (which is NEGATIVE for half the angles) survives the wire with its sign and magnitude.
// A Shape-3 (unsigned-as-widened-signed) regression would flip a left/downward velocity into a huge
// positive here and fail. See net_fixed_wire_compute.{h,cpp}.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include <cmath>
#include <cstdint>

#include "features/fixed64/computation/net_fixed_wire_compute.h"

using zx::WireRoundtripLong;
using zx::WireRoundtripShort;

namespace
{
constexpr int    FRACBITS   = 16;
constexpr int64_t FRACUNIT  = int64_t(1) << FRACBITS;
constexpr int    FINEANGLES = 8192;

// [rc4l] 64-bit fixed multiply, matching the engine's widened FixedMul ((a*b)>>16 in 128-bit).
int64_t fmul(int64_t a, int64_t b) { return int64_t((__int128(a) * b) >> FRACBITS); }

// [rc4l] The engine's finesine fill (r_utility.cpp R_InitTables), the version we corrected:
// double(FRACUNIT)*sin, giving a signed 16.16 sine that is negative across the lower half.
int64_t finesine(int i)
{
	i &= (FINEANGLES - 1);
	return int64_t(double(FRACUNIT) * std::sin(i * (M_PI * 2 / FINEANGLES)));
}
int64_t finecosine(int i) { return finesine(i + FINEANGLES / 4); }
} // namespace

// --- The "long" wire field is exact for any 16.16 value within 32 bits, sign included. ---

TEST(NetFixedWire, LongPreservesValueAndSignInRange)
{
	const int64_t samples[] = {
		0, FRACUNIT, -FRACUNIT, 100 * FRACUNIT, -100 * FRACUNIT,
		FRACUNIT / 2, -FRACUNIT / 2, 32767 * FRACUNIT, -32768 * FRACUNIT,
		1, -1, 0x7FFFFFFFLL, -0x80000000LL, // exact 32-bit extremes
	};
	for (int64_t v : samples)
		EXPECT_EQ(WireRoundtripLong(v), v) << "long wire changed " << v;
}

// --- The "short" wire field keeps the integer map-unit part, with sign, dropping the fraction. ---

TEST(NetFixedWire, ShortKeepsIntegerPartWithSign)
{
	// Positive and negative coordinates within +/-32767 map units.
	EXPECT_EQ(WireRoundtripShort(1015 * FRACUNIT + FRACUNIT / 2), 1015 * FRACUNIT); // fraction dropped
	EXPECT_EQ(WireRoundtripShort(-100 * FRACUNIT), -100 * FRACUNIT);                // sign kept
	EXPECT_EQ(WireRoundtripShort(-1 * FRACUNIT), -1 * FRACUNIT);
	EXPECT_EQ(WireRoundtripShort(0), 0);
	EXPECT_EQ(WireRoundtripShort(32767 * FRACUNIT), 32767 * FRACUNIT);
	// A negative coordinate with a fraction rounds toward -inf (arithmetic >>), matching the engine.
	EXPECT_EQ(WireRoundtripShort(-100 * FRACUNIT - FRACUNIT / 2), -101 * FRACUNIT);
}

// --- End-to-end: a fired projectile's velocity survives the wire at EVERY angle. ---

TEST(NetFixedWire, ProjectileVelocitySurvivesWireAtAllAngles)
{
	// A representative projectile speed (e.g. an imp fireball is 10, a rocket 20; 40 stresses range).
	for (int speedUnits : {10, 20, 40}) // map units per tic
	{
		const int64_t speed = int64_t(speedUnits) * FRACUNIT;
		for (int angle = 0; angle < FINEANGLES; ++angle)
		{
			// Server (P_SpawnMissileAngleZSpeed, level pitch): velx/vely from the trig table.
			const int64_t velx = fmul(speed, finecosine(angle));
			const int64_t vely = fmul(speed, finesine(angle));

			// Wire: SpawnMissile sends velocity as full "long" fields.
			const int64_t velxWire = WireRoundtripLong(velx);
			const int64_t velyWire = WireRoundtripLong(vely);

			// The projectile must still move in the fired direction: exact value, exact sign.
			ASSERT_EQ(velxWire, velx) << "velx corrupted at angle " << angle;
			ASSERT_EQ(velyWire, vely) << "vely corrupted at angle " << angle;
			// And explicitly: a negative component never became a huge positive (Shape-3).
			if (velx < 0) ASSERT_LT(velxWire, 0) << "velx sign flipped at angle " << angle;
			if (vely < 0) ASSERT_LT(velyWire, 0) << "vely sign flipped at angle " << angle;
		}
	}
}
