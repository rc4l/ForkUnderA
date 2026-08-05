// [rc4l] Sprite roll: turning a pair of tic-sampled angle_t rolls into the degrees the renderer
// should draw this frame.
//
// Two things here are easy to get wrong and are the whole reason this is a unit rather than three
// lines inline in gl_sprite.cpp.
//
// SHORTEST WAY ROUND. angle_t wraps, so an actor rolling past 360 degrees goes 0xFF000000 ->
// 0x01000000 between two tics. Interpolating those as plain numbers sweeps the sprite backwards
// through nearly a full turn in one frame. The difference has to be taken as a SIGNED 32-bit
// quantity, which is exactly what makes the wrap behave: 0x01000000 - 0xFF000000 is a small
// positive number in two's complement, so the sprite continues forwards through the seam.
//
// DEGREES, NOT BAM. The GL matrix wants degrees. angle_t is a binary angle where the full turn is
// 2^32, so the scale factor is 360 / 2^32 and the arithmetic has to happen in a type wide enough to
// hold the product -- doing it in 32 bits overflows for any angle past a quarter turn.
//
// Upstream has neither problem because by the time they added +ROLLSPRITE their actor angles were
// already floating point (Angles.Roll.Degrees). Ours are still fixed-point angle_t, so this is the
// adaptation layer.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_SPRITEROLL_COMPUTE_H
#define ZX_SPRITEROLL_COMPUTE_H

#include <stdint.h>

namespace zx
{

// [rc4l] A full turn in angle_t units. Kept as a double so the degree conversion below cannot be
// done accidentally in integer arithmetic.
const double SPRITEROLL_ANGLE_FULL_TURN = 4294967296.0;	// 2^32

// [rc4l] Interpolate between two tic-sampled rolls, taking the shortest way round.
//
// ticFrac is the 16.16 fraction through the current tic, matching r_TicFrac -- 0 means "exactly the
// previous tic", 0x10000 means "exactly the current tic". Values outside that range are clamped, so
// a caller that hands over a forced FRACUNIT (see C_ShouldForceInterpolation) or a stale fraction
// cannot push the sprite past its destination.
inline uint32_t InterpolateSpriteRoll( uint32_t prevRoll, uint32_t roll, int32_t ticFrac )
{
	if ( ticFrac <= 0 )
		return prevRoll;
	if ( ticFrac >= 0x10000 )
		return roll;

	// [rc4l] Take the difference in 64 bits and fold it into (-half turn, +half turn]. That is what
	// makes the 0xFFFFFFFF -> 0x00000000 seam continue FORWARDS instead of sweeping back through
	// nearly the whole circle. Doing this in int32_t arithmetic instead would break the exact
	// half-turn case backwards, because 0x80000000 read as an int32_t is INT32_MIN; folding in 64
	// bits leaves it at +2^31, so a 180 degree flip rotates the way the number went up.
	int64_t delta = static_cast<int64_t>( roll ) - static_cast<int64_t>( prevRoll );

	if ( delta > 0x80000000LL )
		delta -= 0x100000000LL;
	else if ( delta < -0x80000000LL )
		delta += 0x100000000LL;

	const int64_t scaled = ( delta * ticFrac ) >> 16;

	return static_cast<uint32_t>( static_cast<uint32_t>( prevRoll ) + static_cast<uint32_t>( scaled ) );
}

// [rc4l] Convert an angle_t roll to the degrees the GL matrix wants.
inline float SpriteRollToDegrees( uint32_t roll )
{
	return static_cast<float>( static_cast<double>( roll ) * ( 360.0 / SPRITEROLL_ANGLE_FULL_TURN ) );
}

// [rc4l] The whole job: what to hand mat.Rotate for this actor this frame.
inline float ComputeSpriteRollDegrees( uint32_t prevRoll, uint32_t roll, int32_t ticFrac )
{
	return SpriteRollToDegrees( InterpolateSpriteRoll( prevRoll, roll, ticFrac ) );
}

} // namespace zx

#endif // ZX_SPRITEROLL_COMPUTE_H
