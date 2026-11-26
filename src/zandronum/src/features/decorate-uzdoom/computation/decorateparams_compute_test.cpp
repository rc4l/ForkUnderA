// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/decorate-uzdoom/computation/decorateparams_compute.h"

using namespace zx::decorateuzdoom;

TEST(DecorateParams, ExplodeFlagsMapOnlyWhatRadiusAttackReads)
{
	EXPECT_EQ(ComputeExplodeRadiusFlags(0), 0);
	EXPECT_EQ(ComputeExplodeRadiusFlags(kXfHurtSource), kRadfHurtSource);
	EXPECT_EQ(ComputeExplodeRadiusFlags(kXfNotMissile), kRadfSourceIsSpot);
	// [Doruk-Aksoy] XF_EXPLICITDAMAGETYPE shares RADF_NODAMAGE's bit, which must stay clear.
	EXPECT_EQ(ComputeExplodeRadiusFlags(kXfExplicitDamageType | kXfNoSplash), 0);
	EXPECT_EQ(ComputeExplodeRadiusFlags(kXfHurtSource | kXfNotMissile | kXfExplicitDamageType), kRadfHurtSource | kRadfSourceIsSpot);
}

TEST(DecorateParams, ExplodeDamageType)
{
	EXPECT_TRUE(ComputeExplodeUsesOwnDamageType(true, 0));
	EXPECT_FALSE(ComputeExplodeUsesOwnDamageType(true, kXfExplicitDamageType));
	EXPECT_FALSE(ComputeExplodeUsesOwnDamageType(false, 0));
}

TEST(DecorateParams, CloserThanChecksHeightUnlessNoz)
{
	// [Doruk-Aksoy] Self from 0 to 56, target 200 up from 200 to 256: across the ground 50 away.
	EXPECT_FALSE(ComputeCloserThan(50, 0, 56, 200, 256, 100, false));
	EXPECT_TRUE(ComputeCloserThan(50, 0, 56, 200, 256, 100, true));
	EXPECT_TRUE(ComputeCloserThan(50, 0, 56, 100, 156, 100, false));
	// [Doruk-Aksoy] Self above: its feet against the target's top.
	EXPECT_TRUE(ComputeCloserThan(50, 140, 196, 0, 56, 100, false));
	EXPECT_FALSE(ComputeCloserThan(50, 200, 256, 0, 56, 100, false));
	EXPECT_FALSE(ComputeCloserThan(100, 0, 56, 0, 56, 100, true));
}

TEST(DecorateParams, SetScaleY)
{
	EXPECT_EQ(ComputeSetScaleY(2, 0, false), 2);
	EXPECT_EQ(ComputeSetScaleY(2, 0, true), 0);
	EXPECT_EQ(ComputeSetScaleY(2, 3, false), 3);
}

TEST(DecorateParams, HigherOrLower)
{
	// [Doruk-Aksoy] Self from 0 to 56; the other 60 up is above its top, 40 up is not.
	EXPECT_EQ(ComputeHigherOrLower(0, 56, 60, 56, 0, 0, true, true, true), kHigher);
	EXPECT_EQ(ComputeHigherOrLower(0, 56, 40, 56, 0, 0, true, true, true), kNeither);
	EXPECT_EQ(ComputeHigherOrLower(0, 56, 40, 56, 0, 0, false, true, true), kHigher);
	EXPECT_EQ(ComputeHigherOrLower(0, 56, 60, 56, 0, 0, true, false, true), kNeither);
	// [Doruk-Aksoy] The other from -100 to -44 has its top below self's feet.
	EXPECT_EQ(ComputeHigherOrLower(0, 56, -100, 56, 0, 0, true, true, true), kLower);
	EXPECT_EQ(ComputeHigherOrLower(0, 56, -100, 56, 0, -60, true, true, true), kNeither);
	EXPECT_EQ(ComputeHigherOrLower(0, 56, -100, 56, 0, 0, true, true, false), kNeither);
	EXPECT_EQ(ComputeHigherOrLower(0, 56, -30, 56, 0, 0, false, true, true), kLower);
}

TEST(DecorateParams, CheckBlockResult)
{
	EXPECT_FALSE(ComputeCheckBlockResult(true, true, true, kCbfDropoff));
	EXPECT_TRUE(ComputeCheckBlockResult(false, true, false, 0));
	EXPECT_FALSE(ComputeCheckBlockResult(false, true, false, kCbfNoActors));
	EXPECT_TRUE(ComputeCheckBlockResult(false, false, true, 0));
	EXPECT_FALSE(ComputeCheckBlockResult(false, false, true, kCbfNoLines));
	EXPECT_TRUE(ComputeCheckBlockResult(false, false, false, kCbfDropoff));
	EXPECT_FALSE(ComputeCheckBlockResult(false, false, false, 0));
}
