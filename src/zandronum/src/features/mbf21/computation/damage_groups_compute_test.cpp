// [rc4l] Tests for the MBF21 damage-group predicates. Cross-validated against DSDA-Doom's
// P_InfightingImmune / P_ProjectileImmune / P_SplashImmune (the reference implementation).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "features/mbf21/computation/damage_groups_compute.h"

using namespace zx::mbf21;

// ---- infighting: same non-default group -> no retaliation ------------------

TEST(Mbf21Infighting, DefaultGroupNeverImmune)
{
	// IG_DEFAULT vs IG_DEFAULT: vanilla infighting still applies.
	EXPECT_FALSE(ComputeInfightingImmune(IG_DEFAULT, IG_DEFAULT));
	EXPECT_FALSE(ComputeInfightingImmune(IG_DEFAULT, 5));
	EXPECT_FALSE(ComputeInfightingImmune(5, IG_DEFAULT));
}

TEST(Mbf21Infighting, SameNonDefaultGroupImmune)
{
	EXPECT_TRUE(ComputeInfightingImmune(5, 5));
	EXPECT_TRUE(ComputeInfightingImmune(1, 1));
	EXPECT_TRUE(ComputeInfightingImmune(42, 42));
}

TEST(Mbf21Infighting, DifferentGroupsNotImmune)
{
	EXPECT_FALSE(ComputeInfightingImmune(1, 2));
	EXPECT_FALSE(ComputeInfightingImmune(7, 3));
}

// A pair of imps put in infighting group 1 will never turn on each other.
TEST(Mbf21Infighting, SharedGroupMonstersDoNotRetaliate)
{
	const int impGroup = 1;
	EXPECT_TRUE(ComputeInfightingImmune(impGroup, impGroup));
}

// ---- projectile immunity ---------------------------------------------------

TEST(Mbf21Projectile, DefaultSameTypeImmune)
{
	// Vanilla rule: a monster is immune to its own species' projectiles.
	EXPECT_TRUE(ComputeProjectileImmune(PG_DEFAULT, PG_DEFAULT, /*sameType=*/true, /*sameActor=*/false));
}

TEST(Mbf21Projectile, DefaultDifferentTypeNotImmune)
{
	EXPECT_FALSE(ComputeProjectileImmune(PG_DEFAULT, PG_DEFAULT, /*sameType=*/false, false));
}

TEST(Mbf21Projectile, SameNonDefaultGroupImmuneEvenAcrossTypes)
{
	// Two different monster types sharing projectile group 3 don't hurt each other.
	EXPECT_TRUE(ComputeProjectileImmune(3, 3, /*sameType=*/false, false));
}

TEST(Mbf21Projectile, DifferentGroupsNotImmune)
{
	EXPECT_FALSE(ComputeProjectileImmune(3, 4, /*sameType=*/false, false));
	EXPECT_FALSE(ComputeProjectileImmune(3, 4, /*sameType=*/true, false));
}

TEST(Mbf21Projectile, GrouplessDisablesEvenSameSpeciesImmunity)
{
	// PG_GROUPLESS (-1): a groupless monster CAN be hurt by its own species.
	EXPECT_FALSE(ComputeProjectileImmune(PG_GROUPLESS, PG_GROUPLESS, /*sameType=*/true, /*sameActor=*/false));
	EXPECT_FALSE(ComputeProjectileImmune(PG_GROUPLESS, PG_DEFAULT, /*sameType=*/true, false));
}

TEST(Mbf21Projectile, GrouplessButSelfStillImmune)
{
	// Even groupless, a thing is never vulnerable to *itself* (target == source).
	EXPECT_TRUE(ComputeProjectileImmune(PG_GROUPLESS, PG_GROUPLESS, /*sameType=*/false, /*sameActor=*/true));
}

TEST(Mbf21Projectile, DefaultTargetVsGroupedSourceKeysOnTargetAndType)
{
	// Immunity is decided by the TARGET's group: default target + same type -> immune,
	// regardless of the source's group value.
	EXPECT_TRUE(ComputeProjectileImmune(PG_DEFAULT, 9, /*sameType=*/true, false));
	EXPECT_FALSE(ComputeProjectileImmune(PG_DEFAULT, 9, /*sameType=*/false, false));
}

TEST(Mbf21Projectile, NonDefaultTargetVsDefaultSourceNotImmune)
{
	// target in group 5, source default(0): groups differ -> not immune.
	EXPECT_FALSE(ComputeProjectileImmune(5, PG_DEFAULT, /*sameType=*/true, false));
}

// ---- splash immunity (mirrors infighting) ----------------------------------

TEST(Mbf21Splash, DefaultGroupNeverImmune)
{
	EXPECT_FALSE(ComputeSplashImmune(SG_DEFAULT, SG_DEFAULT));
	EXPECT_FALSE(ComputeSplashImmune(SG_DEFAULT, 4));
	EXPECT_FALSE(ComputeSplashImmune(4, SG_DEFAULT));
}

TEST(Mbf21Splash, SameNonDefaultGroupImmune)
{
	EXPECT_TRUE(ComputeSplashImmune(4, 4));
	EXPECT_TRUE(ComputeSplashImmune(2, 2));
}

TEST(Mbf21Splash, DifferentGroupsNotImmune)
{
	EXPECT_FALSE(ComputeSplashImmune(4, 5));
}

// A barrel-explosion (splash group 1) won't chain-detonate other group-1 barrels.
TEST(Mbf21Splash, GroupedBarrelsAreSplashImmuneToEachOther)
{
	const int barrelGroup = 1;
	EXPECT_TRUE(ComputeSplashImmune(barrelGroup, barrelGroup));
}
