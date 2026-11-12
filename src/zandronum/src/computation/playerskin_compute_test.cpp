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

TEST(PlayerSkin, AMorphedBodyKeepsItsSkinUnlessItsClassHasNoSkin)
{
	EXPECT_TRUE(ComputeSkinSpriteApplies(false, false));
	EXPECT_TRUE(ComputeSkinSpriteApplies(false, true));
	EXPECT_TRUE(ComputeSkinSpriteApplies(true, false));
	EXPECT_FALSE(ComputeSkinSpriteApplies(true, true));
}
