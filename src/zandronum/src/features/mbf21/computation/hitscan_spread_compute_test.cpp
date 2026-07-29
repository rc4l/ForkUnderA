// [rc4l] Tests for the MBF21 hitscan-spread math. Values pinned against DSDA-Doom's
// P_RandomHitscanAngle / P_RandomHitscanSlope (m_random.c).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "features/mbf21/computation/hitscan_spread_compute.h"

using namespace zx::mbf21;

// ---- ComputeHitscanAngleBAM: spread_bam * (rnd1 - rnd2) / 255 ---------------

TEST(Mbf21Spread, EqualRollsGiveNoOffset)
{
	EXPECT_EQ(ComputeHitscanAngleBAM(255000, 128, 128), 0);
	EXPECT_EQ(ComputeHitscanAngleBAM(255000, 0, 0), 0);
	EXPECT_EQ(ComputeHitscanAngleBAM(255000, 255, 255), 0);
}

TEST(Mbf21Spread, ExtremesAreExactlyPlusMinusSpread)
{
	// rnd1=255, rnd2=0 -> spread * 255 / 255 = spread
	EXPECT_EQ(ComputeHitscanAngleBAM(255000, 255, 0), 255000);
	// rnd1=0, rnd2=255 -> -spread
	EXPECT_EQ(ComputeHitscanAngleBAM(255000, 0, 255), -255000);
}

TEST(Mbf21Spread, AntisymmetricInTheRolls)
{
	const int64_t s = 123456;
	EXPECT_EQ(ComputeHitscanAngleBAM(s, 200, 40), -ComputeHitscanAngleBAM(s, 40, 200));
}

TEST(Mbf21Spread, ConcreteValueMatchesFormula)
{
	// 255000 * (200-100) / 255 = 255000 * 100 / 255 = 100000
	EXPECT_EQ(ComputeHitscanAngleBAM(255000, 200, 100), 100000);
}

TEST(Mbf21Spread, NegativeSpreadUsesAbsoluteValue)
{
	EXPECT_EQ(ComputeHitscanAngleBAM(-255000, 255, 0), 255000);
	EXPECT_EQ(ComputeHitscanAngleBAM(-255000, 0, 255), -255000);
}

TEST(Mbf21Spread, LargeBamDoesNotOverflow)
{
	// 45 degrees ~ 0x20000000 BAM; * 255 would overflow 32-bit, but int64 keeps it exact.
	const int64_t bam45 = 0x20000000;
	EXPECT_EQ(ComputeHitscanAngleBAM(bam45, 255, 0), (int)bam45);
	EXPECT_EQ(ComputeHitscanAngleBAM(bam45, 0, 255), -(int)bam45);
}

// ---- ComputeHitscanSlopeIndex: (ANG90 - angle) >> 19, with clamps ----------

TEST(Mbf21Slope, ZeroAngleMapsToMidIndex)
{
	// ANG90 (0x40000000) >> 19 = 2048
	EXPECT_EQ(ComputeHitscanSlopeIndex(0), 2048);
}

TEST(Mbf21Slope, PositiveAngleLowersIndex_NegativeRaisesIt)
{
	EXPECT_EQ(ComputeHitscanSlopeIndex(0x20000000), 1024);   // (0x40000000-0x20000000)>>19
	EXPECT_EQ(ComputeHitscanSlopeIndex(-0x20000000), 3072);  // (0x40000000+0x20000000)>>19
}

TEST(Mbf21Slope, ClampsBeyondNinetyDegrees)
{
	EXPECT_EQ(ComputeHitscanSlopeIndex(0x40000001), 0);           // angle > ANG90
	EXPECT_EQ(ComputeHitscanSlopeIndex(-0x40000001), 8192 / 2 - 1); // -angle > ANG90 -> 4095
}

TEST(Mbf21Slope, NinetyDegreeBoundaryIsNotClamped)
{
	EXPECT_EQ(ComputeHitscanSlopeIndex(0x40000000), 0);      // exactly ANG90 -> (0)>>19 = 0
}

// ---- ComputeDegToSlopeIndex (projectile pitch) -----------------------------

static const int64_t DEG = 65536;   // one fixed-point degree

TEST(Mbf21DegToSlope, ZeroPitchIsTheFlatIndex)
{
	// |0| -> ang 0 -> (ANG90 - 0) >> 19 = 0x40000000 >> 19 = 2048 (tangent 0 / horizontal).
	EXPECT_EQ(ComputeDegToSlopeIndex(0), 2048);
}

TEST(Mbf21DegToSlope, FortyFiveDegreesHalvesTheOffset)
{
	// 45 deg -> ang ~= 0x20000000 -> (0x40000000 - ang) >> 19 = 1024.
	EXPECT_EQ(ComputeDegToSlopeIndex(45 * DEG), 1024);
}

TEST(Mbf21DegToSlope, SignIsIgnored_MagnitudeOnly)
{
	// The helper returns the same index for +/- pitch; the engine re-applies the sign to the slope.
	EXPECT_EQ(ComputeDegToSlopeIndex(-45 * DEG), ComputeDegToSlopeIndex(45 * DEG));
	EXPECT_EQ(ComputeDegToSlopeIndex(-45 * DEG), 1024);
}

TEST(Mbf21DegToSlope, BeyondNinetyDegreesClampsToTheSteepestIndex)
{
	// 91 deg pushes ang past ANG90, so it clamps to ANG90-1 -> index 0 (steepest downward).
	EXPECT_EQ(ComputeDegToSlopeIndex(91 * DEG), 0);
	EXPECT_EQ(ComputeDegToSlopeIndex(-120 * DEG), 0);
}
