// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/playerskin_compute.h"

#include <gtest/gtest.h>

using namespace zx::skin;

TEST(PlayerSkin, ClSkinsZeroForbidsEverySkin)
{
	EXPECT_TRUE(ComputeClSkinsForbids(0, false));
	EXPECT_TRUE(ComputeClSkinsForbids(-1, false));
}

TEST(PlayerSkin, ClSkinsOneAllowsEverySkin)
{
	EXPECT_FALSE(ComputeClSkinsForbids(1, false));
	EXPECT_FALSE(ComputeClSkinsForbids(1, true));
}

TEST(PlayerSkin, ClSkinsTwoForbidsOnlyCheatSkins)
{
	EXPECT_TRUE(ComputeClSkinsForbids(2, true));
	EXPECT_FALSE(ComputeClSkinsForbids(2, false));
	EXPECT_TRUE(ComputeClSkinsForbids(3, true));
}

TEST(PlayerSkin, AnAllowedSkinInRangeIsShown)
{
	EXPECT_EQ(3, ComputeShownSkin(3, 5, false, 0));
}

TEST(PlayerSkin, AForbiddenOrOutOfRangeSkinShowsTheBase)
{
	EXPECT_EQ(1, ComputeShownSkin(3, 5, true, 1));
	EXPECT_EQ(1, ComputeShownSkin(5, 5, false, 1));
	EXPECT_EQ(1, ComputeShownSkin(-1, 5, false, 1));
}

TEST(PlayerSkin, ABodyShowingItsSpawnSpriteTakesTheSkin)
{
	EXPECT_EQ(SKIN_SPRITE_SKIN, ComputeSkinSprite(false, true, false, false));
}

TEST(PlayerSkin, ABodyShowingAnotherSpriteOrNothingKeepsIt)
{
	// [rc4l] A morph, or a state that changed the sprite, is left as it is.
	EXPECT_EQ(SKIN_SPRITE_KEEP, ComputeSkinSprite(false, false, false, false));
	EXPECT_EQ(SKIN_SPRITE_KEEP, ComputeSkinSprite(false, true, true, false));
}

TEST(PlayerSkin, NoSkinPutsBackTheStatesSpriteForASkins)
{
	EXPECT_EQ(SKIN_SPRITE_STATE, ComputeSkinSprite(true, true, false, true));
	EXPECT_EQ(SKIN_SPRITE_KEEP, ComputeSkinSprite(true, true, false, false));
}

TEST(DrawnSkinSprite, ABodyShowingItsSpawnSpriteIsDrawnInItsSkin)
{
	EXPECT_EQ(zx::skin::ComputeDrawnSkinSprite(10, 42, true, false), 42);
	EXPECT_EQ(zx::skin::ComputeDrawnSkinSprite(42, 42, true, false), 42);
}

TEST(DrawnSkinSprite, AMorphOrAnEmptyStateKeepsItsSprite)
{
	EXPECT_EQ(zx::skin::ComputeDrawnSkinSprite(10, 42, false, false), 10);
	EXPECT_EQ(zx::skin::ComputeDrawnSkinSprite(10, 42, true, true), 10);
}
