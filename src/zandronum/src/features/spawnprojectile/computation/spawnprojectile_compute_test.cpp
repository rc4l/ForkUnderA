// [rc4l] Tests for the A_SpawnProjectile / A_CustomMissile pitch split.
//
// The property that matters: with CMF_BADPITCH the unit must reproduce A_CustomMissile's historic
// (inverted) arithmetic BIT FOR BIT, and without it must match upstream's corrected form. A mod
// aiming with A_CustomMissile must not change behaviour, and a mod aiming with A_SpawnProjectile
// must not inherit the inversion.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"
#include "features/spawnprojectile/computation/spawnprojectile_compute.h"

using namespace zx;

namespace
{
// [rc4l] 16.16 helpers mirroring the engine's, so the expectations below read in map units.
constexpr int64_t kOne = 1 << 16;
inline int64_t Fx(double v) { return (int64_t)(v * kOne); }
inline int64_t FixedMulBits(int64_t a, int64_t b) { return (a * b) >> 16; }

// [rc4l] angle_t: the full circle is 2^32, so 90 degrees is 2^30.
constexpr uint32_t kAng90 = 0x40000000u;
constexpr uint32_t kAng45 = 0x20000000u;
}

// ---------------------------------------------------------------------------------------------
// SpawnProjectileUsesPitch
// ---------------------------------------------------------------------------------------------
TEST(SpawnProjectilePitch, PitchAppliesOnlyForAbsoluteOrOffset)
{
	EXPECT_FALSE(SpawnProjectileUsesPitch(0));
	EXPECT_TRUE(SpawnProjectileUsesPitch(ZX_CMF_ABSOLUTEPITCH));
	EXPECT_TRUE(SpawnProjectileUsesPitch(ZX_CMF_OFFSETPITCH));
	EXPECT_TRUE(SpawnProjectileUsesPitch(ZX_CMF_ABSOLUTEPITCH | ZX_CMF_OFFSETPITCH));
	// BADPITCH alone selects a formula; it does not by itself make the pitch apply.
	EXPECT_FALSE(SpawnProjectileUsesPitch(ZX_CMF_BADPITCH));
}

// ---------------------------------------------------------------------------------------------
// ComputeSpawnProjectilePitch
// ---------------------------------------------------------------------------------------------
TEST(SpawnProjectilePitch, WithoutOffsetThePitchIsUsedAsGiven)
{
	// No CMF_OFFSETPITCH: the missile's own velocity pitch is irrelevant in both modes.
	EXPECT_EQ(ComputeSpawnProjectilePitch(ZX_CMF_ABSOLUTEPITCH, kAng45, kAng90), kAng45);
	EXPECT_EQ(ComputeSpawnProjectilePitch(ZX_CMF_ABSOLUTEPITCH | ZX_CMF_BADPITCH, kAng45, kAng90),
	          kAng45);
}

TEST(SpawnProjectilePitch, OffsetSignIsInvertedByBadPitch)
{
	const uint32_t pitch = kAng45, vel = 0x08000000u; // vel = 22.5 degrees

	// Corrected (A_SpawnProjectile): subtract the missile's velocity pitch.
	EXPECT_EQ(ComputeSpawnProjectilePitch(ZX_CMF_OFFSETPITCH, pitch, vel), pitch - vel);
	// Historic (A_CustomMissile): add it. This is upstream's "bogus calculation".
	EXPECT_EQ(ComputeSpawnProjectilePitch(ZX_CMF_OFFSETPITCH | ZX_CMF_BADPITCH, pitch, vel),
	          pitch + vel);
	// The two modes must genuinely disagree whenever there is an offset to apply.
	EXPECT_NE(ComputeSpawnProjectilePitch(ZX_CMF_OFFSETPITCH, pitch, vel),
	          ComputeSpawnProjectilePitch(ZX_CMF_OFFSETPITCH | ZX_CMF_BADPITCH, pitch, vel));
}

TEST(SpawnProjectilePitch, ZeroOffsetLeavesBothModesIdentical)
{
	// A missile with no vertical velocity component contributes nothing, so the historic and
	// corrected forms agree -- the inversion is invisible in the common flat-shot case, which is
	// exactly why it survived so long.
	EXPECT_EQ(ComputeSpawnProjectilePitch(ZX_CMF_OFFSETPITCH, kAng45, 0), kAng45);
	EXPECT_EQ(ComputeSpawnProjectilePitch(ZX_CMF_OFFSETPITCH | ZX_CMF_BADPITCH, kAng45, 0), kAng45);
}

TEST(SpawnProjectilePitch, AngleArithmeticWrapsModulo2Pow32)
{
	// angle_t is modular; subtracting past zero must wrap rather than clamp or trap.
	EXPECT_EQ(ComputeSpawnProjectilePitch(ZX_CMF_OFFSETPITCH, 0u, kAng90), (uint32_t)(0u - kAng90));
	// And adding past a full circle wraps too.
	EXPECT_EQ(ComputeSpawnProjectilePitch(ZX_CMF_OFFSETPITCH | ZX_CMF_BADPITCH, 0xF0000000u,
	                                      0x20000000u),
	          (uint32_t)0x10000000u);
}

// ---------------------------------------------------------------------------------------------
// ComputeSpawnProjectileVelocity
// ---------------------------------------------------------------------------------------------
TEST(SpawnProjectileVelocityTest, VerticalSignIsInvertedByBadPitch)
{
	const int64_t sin = Fx(0.5), cos = Fx(0.866), speed = Fx(20);

	const SpawnProjectileVelocity good = ComputeSpawnProjectileVelocity(ZX_CMF_ABSOLUTEPITCH, sin,
	                                                                    cos, speed);
	const SpawnProjectileVelocity bad = ComputeSpawnProjectileVelocity(
		ZX_CMF_ABSOLUTEPITCH | ZX_CMF_BADPITCH, sin, cos, speed);

	// Corrected form drives velz with -sin; the historic one with +sin.
	EXPECT_EQ(good.velZ, -FixedMulBits(sin, speed));
	EXPECT_EQ(bad.velZ, FixedMulBits(sin, speed));
	EXPECT_EQ(good.velZ, -bad.velZ);
	// The horizontal component is the same in both -- only the vertical sign differs.
	EXPECT_EQ(good.speedXY, bad.speedXY);
}

TEST(SpawnProjectileVelocityTest, HorizontalSpeedIsAbsolute)
{
	// Past vertical the cosine goes negative. |cos * Speed| keeps the missile travelling forwards
	// instead of reversing its horizontal direction.
	const int64_t speed = Fx(20);
	const SpawnProjectileVelocity out = ComputeSpawnProjectileVelocity(ZX_CMF_ABSOLUTEPITCH, 0,
	                                                                   Fx(-0.866), speed);
	EXPECT_GT(out.speedXY, 0);
	EXPECT_EQ(out.speedXY, FixedMulBits(Fx(0.866), speed));
}

TEST(SpawnProjectileVelocityTest, LevelShotHasNoVerticalComponent)
{
	// sin(0) == 0, so a level shot has zero vertical velocity in both modes -- and negating zero
	// must not produce anything odd.
	const int64_t speed = Fx(20);
	for (int flags : {(int)ZX_CMF_ABSOLUTEPITCH, (int)(ZX_CMF_ABSOLUTEPITCH | ZX_CMF_BADPITCH)})
	{
		const SpawnProjectileVelocity out = ComputeSpawnProjectileVelocity(flags, 0, kOne, speed);
		EXPECT_EQ(out.velZ, 0);
		EXPECT_EQ(out.speedXY, speed);
	}
}

TEST(SpawnProjectileVelocityTest, StraightUpPutsAllSpeedIntoVertical)
{
	// cos(90) == 0, sin(90) == 1: the whole speed goes vertical, none horizontal.
	const int64_t speed = Fx(20);
	const SpawnProjectileVelocity good = ComputeSpawnProjectileVelocity(ZX_CMF_ABSOLUTEPITCH, kOne,
	                                                                    0, speed);
	EXPECT_EQ(good.speedXY, 0);
	EXPECT_EQ(good.velZ, -speed);

	const SpawnProjectileVelocity bad = ComputeSpawnProjectileVelocity(
		ZX_CMF_ABSOLUTEPITCH | ZX_CMF_BADPITCH, kOne, 0, speed);
	EXPECT_EQ(bad.velZ, speed);
}

TEST(SpawnProjectileVelocityTest, NegativeSineIsHandled)
{
	// A downward pitch: the corrected form must produce the opposite sign to the historic one here
	// too, not merely for upward pitches.
	const int64_t sin = Fx(-0.5), speed = Fx(20);
	EXPECT_EQ(ComputeSpawnProjectileVelocity(ZX_CMF_ABSOLUTEPITCH, sin, kOne, speed).velZ,
	          -FixedMulBits(sin, speed));
	EXPECT_EQ(ComputeSpawnProjectileVelocity(ZX_CMF_ABSOLUTEPITCH | ZX_CMF_BADPITCH, sin, kOne,
	                                         speed).velZ,
	          FixedMulBits(sin, speed));
}

TEST(SpawnProjectileVelocityTest, WideFixedValuesDoNotTruncate)
{
	// fixed_t is 64-bit (48.16) here. A speed far past what a 32-bit fixed could hold must survive
	// the multiply -- this is the fixed64 trap the widening skill warns about.
	const int64_t speed = (int64_t)100000 * kOne; // ~6.5e9 raw, well past INT32_MAX
	const SpawnProjectileVelocity out = ComputeSpawnProjectileVelocity(ZX_CMF_ABSOLUTEPITCH, 0,
	                                                                   kOne, speed);
	EXPECT_EQ(out.speedXY, speed);
	EXPECT_GT(out.speedXY, (int64_t)0x7FFFFFFF);
}
