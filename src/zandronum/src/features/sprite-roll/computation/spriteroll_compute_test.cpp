// [rc4l] Tests for the sprite-roll interpolation math.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"
#include "spriteroll_compute.h"

using namespace zx;

// [rc4l] angle_t landmarks, spelled out so the expectations read as angles rather than hex.
static const uint32_t ROLL_0	= 0x00000000u;
static const uint32_t ROLL_90	= 0x40000000u;
static const uint32_t ROLL_180	= 0x80000000u;
static const uint32_t ROLL_270	= 0xC0000000u;
static const int32_t  HALF_TIC	= 0x8000;
static const int32_t  FULL_TIC	= 0x10000;

TEST(SpriteRoll, EndpointsAreExact)
{
	// A zero fraction must render the previous tic untouched and a full fraction the current one --
	// no rounding drift at either end, or a stationary sprite would shimmer.
	EXPECT_EQ(InterpolateSpriteRoll(ROLL_90, ROLL_270, 0), ROLL_90);
	EXPECT_EQ(InterpolateSpriteRoll(ROLL_90, ROLL_270, FULL_TIC), ROLL_270);
}

TEST(SpriteRoll, OutOfRangeFractionsClampRatherThanOvershoot)
{
	// C_ShouldForceInterpolation hands over a forced FRACUNIT, and a stale frame can present a
	// fraction past the end of the tic. Neither may push the sprite beyond its destination.
	EXPECT_EQ(InterpolateSpriteRoll(ROLL_0, ROLL_90, -1), ROLL_0);
	EXPECT_EQ(InterpolateSpriteRoll(ROLL_0, ROLL_90, FULL_TIC * 4), ROLL_90);
}

TEST(SpriteRoll, HalfwayIsHalfway)
{
	EXPECT_EQ(InterpolateSpriteRoll(ROLL_0, ROLL_180, HALF_TIC), ROLL_90);
	EXPECT_EQ(InterpolateSpriteRoll(ROLL_90, ROLL_270, HALF_TIC), ROLL_180);
}

TEST(SpriteRoll, WrappingForwardsTakesTheShortWayRound)
{
	// The whole reason this is a unit. An actor rolling forwards past 360 goes from just under a
	// full turn to just over it; treated as plain numbers that sweeps backwards through nearly the
	// entire circle in one frame.
	const uint32_t justBefore = 0xFF000000u;	// 358.6 degrees
	const uint32_t justAfter  = 0x01000000u;	// 1.4 degrees, i.e. forwards through the seam

	// The forward distance is 2.8 degrees, so halfway lands exactly on the seam itself.
	EXPECT_EQ(InterpolateSpriteRoll(justBefore, justAfter, HALF_TIC), ROLL_0);

	// A quarter of the way is still short of the seam; three quarters is past it. Naive unsigned
	// subtraction would instead sweep backwards through nearly the whole circle, putting the
	// quarter point somewhere around 270 degrees.
	EXPECT_GT(InterpolateSpriteRoll(justBefore, justAfter, HALF_TIC / 2), justBefore);
	EXPECT_LT(InterpolateSpriteRoll(justBefore, justAfter, HALF_TIC + HALF_TIC / 2), justAfter);
}

TEST(SpriteRoll, WrappingBackwardsAlsoTakesTheShortWayRound)
{
	const uint32_t justAfter  = 0x01000000u;
	const uint32_t justBefore = 0xFF000000u;

	// Rolling backwards through zero: halfway lands on the seam from the other side.
	EXPECT_EQ(InterpolateSpriteRoll(justAfter, justBefore, HALF_TIC), ROLL_0);

	// A quarter of the way back is still above zero, not down near 90 degrees where an unsigned
	// reading of the difference would have put it.
	EXPECT_LT(InterpolateSpriteRoll(justAfter, justBefore, HALF_TIC / 2), justAfter);
}

TEST(SpriteRoll, DegreeConversionCoversTheWholeTurn)
{
	EXPECT_FLOAT_EQ(SpriteRollToDegrees(ROLL_0), 0.0f);
	EXPECT_FLOAT_EQ(SpriteRollToDegrees(ROLL_90), 90.0f);
	EXPECT_FLOAT_EQ(SpriteRollToDegrees(ROLL_180), 180.0f);
	EXPECT_FLOAT_EQ(SpriteRollToDegrees(ROLL_270), 270.0f);
}

TEST(SpriteRoll, DegreeConversionDoesNotOverflowPastAQuarterTurn)
{
	// Doing the 360/2^32 scaling in 32-bit arithmetic overflows above ROLL_90 and yields a negative
	// or wrapped angle. Anything past a quarter turn must still come out in [0, 360).
	EXPECT_GT(SpriteRollToDegrees(ROLL_270), 269.0f);
	// The very largest angle_t is 359.99999992 degrees, which single precision rounds to exactly
	// 360 -- harmless, since rotating a billboard by 360 is rotating it by 0, but worth pinning so
	// nobody "fixes" it into a wrap.
	EXPECT_LE(SpriteRollToDegrees(0xFFFFFFFFu), 360.0f);
	EXPECT_GT(SpriteRollToDegrees(0xFFFFFFFFu), 359.0f);
}

TEST(SpriteRoll, TheCombinedEntryPointAgreesWithItsParts)
{
	EXPECT_FLOAT_EQ(ComputeSpriteRollDegrees(ROLL_0, ROLL_180, HALF_TIC), 90.0f);
	EXPECT_FLOAT_EQ(ComputeSpriteRollDegrees(ROLL_90, ROLL_90, HALF_TIC), 90.0f);
}
