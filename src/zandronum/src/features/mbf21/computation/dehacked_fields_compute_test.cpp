// [rc4l] Tests for the MBF21 DeHackEd field conversions, including end-to-end checks that the stored
// encoding feeds the damage-group predicates correctly. Encoding pinned to DSDA-Doom d_deh.c.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "features/mbf21/computation/dehacked_fields_compute.h"
#include "features/mbf21/computation/damage_groups_compute.h"

using namespace zx::mbf21;

static const int64_t FRACUNIT = 65536;

// ---- infighting group encoding ---------------------------------------------

TEST(Mbf21DehFields, InfightingGroupOffsetsPastDefault)
{
	EXPECT_EQ(ComputeInfightingGroupStored(0), IG_END);       // DEH 0 -> 1, a real group
	EXPECT_EQ(ComputeInfightingGroupStored(5), 5 + IG_END);
	EXPECT_NE(ComputeInfightingGroupStored(0), IG_DEFAULT);   // never collides with "unset"
}

// ---- projectile group encoding (negative = groupless) ----------------------

TEST(Mbf21DehFields, ProjectileGroupNegativeIsGroupless)
{
	EXPECT_EQ(ComputeProjectileGroupStored(-1), PG_GROUPLESS);
	EXPECT_EQ(ComputeProjectileGroupStored(-99), PG_GROUPLESS);
}

TEST(Mbf21DehFields, ProjectileGroupOffsetsPastBuiltins)
{
	EXPECT_EQ(ComputeProjectileGroupStored(0), PG_END);       // DEH 0 -> 2 (past DEFAULT/BARON)
	EXPECT_EQ(ComputeProjectileGroupStored(3), 3 + PG_END);
	EXPECT_NE(ComputeProjectileGroupStored(0), PG_DEFAULT);
	EXPECT_NE(ComputeProjectileGroupStored(0), PG_BARON);
}

// ---- splash group encoding -------------------------------------------------

TEST(Mbf21DehFields, SplashGroupOffsetsPastDefault)
{
	EXPECT_EQ(ComputeSplashGroupStored(0), SG_END);
	EXPECT_EQ(ComputeSplashGroupStored(7), 7 + SG_END);
	EXPECT_NE(ComputeSplashGroupStored(0), SG_DEFAULT);
}

// ---- melee range convention ------------------------------------------------

TEST(Mbf21DehFields, MeleeRangeDropsTwentyUnitRadius)
{
	// DEH 64.0 (the vanilla MELEERANGE) -> 44.0 in ZDoom's radius-excluding convention.
	EXPECT_EQ(ComputeMeleeRangeFixed(64 * FRACUNIT), 44 * FRACUNIT);
	EXPECT_EQ(ComputeMeleeRangeFixed(0), -20 * FRACUNIT);
	EXPECT_EQ(ComputeMeleeRangeFixed(100 * FRACUNIT), 80 * FRACUNIT);
}

// ---- end-to-end: encoded groups drive the immunity predicates --------------

TEST(Mbf21DehFields, EncodedGroupZeroGrantsImmunity)
{
	// Two things given DEH "Infighting group 0" must not retaliate against each other.
	const int g = ComputeInfightingGroupStored(0);
	EXPECT_TRUE(ComputeInfightingImmune(g, g));
}

TEST(Mbf21DehFields, EncodedProjectileGroupSharedAcrossTypes)
{
	const int g = ComputeProjectileGroupStored(4);
	EXPECT_TRUE(ComputeProjectileImmune(g, g, /*sameType=*/false, /*sameActor=*/false));
}

TEST(Mbf21DehFields, GrouplessThingNotImmuneToOwnSpecies)
{
	const int g = ComputeProjectileGroupStored(-1);   // PG_GROUPLESS
	EXPECT_FALSE(ComputeProjectileImmune(g, g, /*sameType=*/true, /*sameActor=*/false));
}

TEST(Mbf21DehFields, EncodedSplashGroupBlocksChainExplosions)
{
	const int g = ComputeSplashGroupStored(1);
	EXPECT_TRUE(ComputeSplashImmune(g, g));
}
