// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "precache_compute.h"

#include <gtest/gtest.h>

#include <cstdint>

using namespace zx::hwrender;

// [rc4l] The backend's precache was a no-op, so every sprite a fight first showed was decoded and uploaded on that frame.
TEST(Precache, WorldHitsAskForThePlainMaterialAndSpritesForTheExpandedOne)
{
	const PrecacheAsk wall = ComputePrecacheAsk(kHitWall, 2);
	EXPECT_TRUE(wall.plain);
	EXPECT_FALSE(wall.expanded);
	EXPECT_TRUE(ComputePrecacheAsk(kHitFlat, 1).plain);
	EXPECT_TRUE(ComputePrecacheAsk(kHitSky, 1).plain);
	const PrecacheAsk sprite = ComputePrecacheAsk(kHitSprite, 2);
	EXPECT_FALSE(sprite.plain);
	EXPECT_TRUE(sprite.expanded);
	const PrecacheAsk both = ComputePrecacheAsk(kHitWall | kHitSprite, 2);
	EXPECT_TRUE(both.plain && both.expanded);
}

TEST(Precache, CachingOffAsksForNothingAsGLMakesNothingThen)
{
	const PrecacheAsk off = ComputePrecacheAsk(kHitWall | kHitSprite, 0);
	EXPECT_FALSE(off.plain || off.expanded);
}

TEST(Precache, ATextureMarkedTwiceIsQueuedOnceWithBothMarks)
{
	PrecacheQueue q;
	int a = 0, b = 0;
	ComputePrecacheQueueAdd(q, &a, kHitSprite);
	ComputePrecacheQueueAdd(q, &b, 0);
	ComputePrecacheQueueAdd(q, nullptr, kHitWall);
	ComputePrecacheQueueAdd(q, &a, kHitWall);
	ASSERT_EQ(1u, q.entries.size());
	EXPECT_EQ(kHitSprite | kHitWall, q.entries[0].hits);
}

TEST(Precache, TheQueueIsTakenWorldFirstAndOtherwiseInOrderAndLeftEmpty)
{
	PrecacheQueue q;
	int s1 = 0, w1 = 0, s2 = 0, w2 = 0;
	ComputePrecacheQueueAdd(q, &s1, kHitSprite);
	ComputePrecacheQueueAdd(q, &w1, kHitFlat);
	ComputePrecacheQueueAdd(q, &s2, kHitSprite);
	ComputePrecacheQueueAdd(q, &w2, kHitSky | kHitSprite);
	const std::vector<PrecacheEntry> taken = ComputePrecacheQueueTake(q);
	ASSERT_EQ(4u, taken.size());
	EXPECT_EQ(&w1, taken[0].texture);
	EXPECT_EQ(&w2, taken[1].texture);
	EXPECT_EQ(&s1, taken[2].texture);
	EXPECT_EQ(&s2, taken[3].texture);
	EXPECT_TRUE(q.entries.empty());
	ComputePrecacheQueueAdd(q, &s1, kHitSprite);
	EXPECT_EQ(1u, q.entries.size());
}

// [rc4l] A mark's graphic was in no hitlist, so the first chip of a fight was decoded on its frame.
TEST(Precache, ADecalGraphicIsQueuedOncePerTranslationAndShading)
{
	PrecacheQueue q;
	int chip = 0, blood = 0;
	ComputePrecacheQueueAddDecal(q, &chip, 0, true);
	ComputePrecacheQueueAddDecal(q, &chip, 0, true);
	ComputePrecacheQueueAddDecal(q, &chip, 0, false);
	ComputePrecacheQueueAddDecal(q, &blood, 5, false);
	ComputePrecacheQueueAddDecal(q, nullptr, 0, false);
	const std::vector<DecalPrecache> taken = ComputePrecacheQueueTakeDecals(q);
	ASSERT_EQ(3u, taken.size());
	EXPECT_EQ(&chip, taken[0].texture);
	EXPECT_TRUE(taken[0].redToAlpha);
	EXPECT_FALSE(taken[1].redToAlpha);
	EXPECT_EQ(5, taken[2].translation);
	EXPECT_TRUE(q.decals.empty());
	ComputePrecacheQueueAddDecal(q, &chip, 0, true);
	EXPECT_EQ(1u, q.decals.size());
}

TEST(Precache, AShadedDecalOnTheBasePaletteIsAskedForWithTheMaskTranslation)
{
	EXPECT_EQ(77, ComputeDecalPassTranslation(3, true, true, 77));
	EXPECT_EQ(3, ComputeDecalPassTranslation(3, true, false, 77));
	EXPECT_EQ(3, ComputeDecalPassTranslation(3, false, true, 77));
}

TEST(Precache, ABrowserMakesAheadUpToItsBudgetAndANativeDeviceEverything)
{
	EXPECT_EQ(kBrowserPrecacheBytes, ComputePrecacheBudget(true));
	EXPECT_EQ(SIZE_MAX, ComputePrecacheBudget(false));
}

TEST(Precache, AMaterialsBytesGainAThirdForItsMips)
{
	EXPECT_EQ(64u * 64u * 4u, ComputeMaterialBytes(64, 64, false));
	EXPECT_EQ(64u * 64u * 4u + 64u * 64u * 4u / 3u, ComputeMaterialBytes(64, 64, true));
	EXPECT_EQ(0u, ComputeMaterialBytes(0, 64, true));
	EXPECT_EQ(0u, ComputeMaterialBytes(64, -1, true));
}

TEST(Precache, SpendingStopsAtTheBudgetWhileSmallerOnesStillFit)
{
	size_t spent = 0;
	EXPECT_TRUE(ComputePrecacheSpend(spent, 60, 100));
	EXPECT_FALSE(ComputePrecacheSpend(spent, 50, 100));
	EXPECT_EQ(60u, spent);
	EXPECT_TRUE(ComputePrecacheSpend(spent, 40, 100));
	EXPECT_FALSE(ComputePrecacheSpend(spent, 1, 100));
	size_t over = 120;
	EXPECT_FALSE(ComputePrecacheSpend(over, 0, 100));
	size_t none = 0;
	EXPECT_TRUE(ComputePrecacheSpend(none, 1000, SIZE_MAX));
}
