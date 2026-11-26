// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/decorate-uzdoom/computation/decorateprops_compute.h"
#include "features/mbf21/computation/dehacked_fields_compute.h"

using namespace zx::decorateuzdoom;

TEST(DecorateProps, GroupZeroIsNoGroup)
{
	int stored = 99;
	EXPECT_TRUE(ComputeDecorateGroupStored(kInfightingGroup, 0, &stored));
	EXPECT_EQ(stored, 0);
	EXPECT_TRUE(ComputeDecorateGroupStored(kProjectileGroup, 0, &stored));
	EXPECT_EQ(stored, 0);
	EXPECT_TRUE(ComputeDecorateGroupStored(kSplashGroup, 0, &stored));
	EXPECT_EQ(stored, 0);
}

TEST(DecorateProps, PositiveGroupsMatchDehacked)
{
	int stored = 0;
	EXPECT_TRUE(ComputeDecorateGroupStored(kInfightingGroup, 3, &stored));
	EXPECT_EQ(stored, zx::mbf21::ComputeInfightingGroupStored(3));
	EXPECT_TRUE(ComputeDecorateGroupStored(kProjectileGroup, 3, &stored));
	EXPECT_EQ(stored, zx::mbf21::ComputeProjectileGroupStored(3));
	EXPECT_TRUE(ComputeDecorateGroupStored(kSplashGroup, 3, &stored));
	EXPECT_EQ(stored, zx::mbf21::ComputeSplashGroupStored(3));
}

TEST(DecorateProps, ProjectileMinusOneIsGroupless)
{
	int stored = 0;
	EXPECT_TRUE(ComputeDecorateGroupStored(kProjectileGroup, -1, &stored));
	EXPECT_EQ(stored, zx::mbf21::PG_GROUPLESS);
}

TEST(DecorateProps, NegativeGroupsAreRefused)
{
	int stored = 7;
	EXPECT_FALSE(ComputeDecorateGroupStored(kInfightingGroup, -1, &stored));
	EXPECT_FALSE(ComputeDecorateGroupStored(kSplashGroup, -1, &stored));
	EXPECT_FALSE(ComputeDecorateGroupStored(kProjectileGroup, -2, &stored));
	EXPECT_EQ(stored, 7);
}

TEST(DecorateProps, RespawnTics)
{
	EXPECT_EQ(ComputeRespawnTics(420, 0, 35), 420);
	EXPECT_EQ(ComputeRespawnTics(420, 100, 35), 100);
	EXPECT_EQ(ComputeRespawnTics(420, -3, 35), 105);
}

TEST(DecorateProps, SelfDamage)
{
	EXPECT_EQ(ComputeSelfDamage(100, 0.5, false), 50);
	EXPECT_EQ(ComputeSelfDamage(7, 0.5, false), 3);
	EXPECT_EQ(ComputeSelfDamage(100, 0, false), 0);
	EXPECT_EQ(ComputeSelfDamage(1000000, 0.5, true), 1000000);
}
