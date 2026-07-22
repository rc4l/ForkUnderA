// [rc4l] Reproduction for "plasma/projectiles spawn at feet when aiming above 0 degrees" (netplay).
// server_ClientMove (sv_main.cpp) does:  player->mo->pitch = fixed_t( moveCmd.pitch );
// where CLIENT_MOVE_COMMAND_s::pitch is angle_t (UNSIGNED). Aiming up is a NEGATIVE pitch, which
// travels in the unsigned field as its two's-complement bit pattern (e.g. 0xF8000000). With the
// strong Fixed type, Fixed(unsigned) is the EXPLICIT, ZERO-extending ctor -- so the negative aim-up
// pitch becomes a huge POSITIVE 64-bit value, the shot fires downward, and the projectile lands at
// the shooter's feet. Aim-down (a small positive pitch) is unaffected, which is why "aim down works".
// Upstream Zandronum kept fixed_t 32-bit, so (fixed_t)angle_t reinterpreted the bits as signed.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include <cstdint>

#include "features/fixed64/computation/fixed_strong.h"

using zx::Fixed;
using angle_t = uint32_t;

namespace
{
// [rc4l] tables.h: ANGLE_1 = (ANGLE_45/45), ANGLE_45 = 0x20000000. One BAM degree of pitch.
constexpr int64_t ANGLE_1 = 0x20000000 / 45;
} // namespace

TEST(NetPitch, AimUpPitchMustStayNegativeThroughTheWire)
{
	// The shooter aims ~11 degrees UP: mo->pitch is negative in this codebase's BAM convention.
	const int64_t aimUp = -(11 * ANGLE_1);
	ASSERT_LT(aimUp, 0);

	// The pitch crosses the wire in an angle_t (unsigned) field -- its two's-complement bits.
	const angle_t wire = static_cast<angle_t>(aimUp);

	// server_ClientMove reconstructs it. The reconstruction must recover the NEGATIVE aim-up pitch,
	// so the missile still fires upward. If it comes back positive, the shot goes into the floor.
	const Fixed reconstructed = Fixed(static_cast<int32_t>(wire)); // reinterpret 32-bit as signed
	EXPECT_EQ(reconstructed.Raw(), aimUp);
	EXPECT_LT(reconstructed.Raw(), 0) << "aim-up pitch flipped positive -> projectile fires down";

	// Aim-down (positive pitch) is symmetric and must also round-trip.
	const int64_t aimDown = 11 * ANGLE_1;
	EXPECT_EQ(Fixed(static_cast<int32_t>(static_cast<angle_t>(aimDown))).Raw(), aimDown);
}

// [rc4l] Pin the exact defect: the raw Fixed(unsigned) conversion that the code used zero-extends,
// which is WRONG for a value that represents a signed angle. This documents why the fix must
// reinterpret through a signed 32-bit type first.
TEST(NetPitch, FixedFromUnsignedZeroExtendsAndIsWrongForPitch)
{
	const int64_t aimUp = -(11 * ANGLE_1);
	const angle_t wire = static_cast<angle_t>(aimUp);

	const Fixed buggy = Fixed(wire); // Fixed(unsigned) -- the explicit zero-extending ctor
	EXPECT_GT(buggy.Raw(), 0) << "zero-extension turns the aim-up pitch into a large positive";
	EXPECT_NE(buggy.Raw(), aimUp) << "which is not the pitch the player aimed";
}
