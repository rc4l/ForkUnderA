// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/spriteclip_compute.h"

#include <gtest/gtest.h>

using namespace zx::sprites;

namespace {

SpriteClipIn Monster(float bottom)
{
	SpriteClipIn in = {};
	in.living = true;
	in.monster = true;
	in.spriteClip = 1;
	in.clipFactor = 1.8f;
	in.clipThreshold = 10.f;
	in.bottom = bottom;
	in.top = 1000.f;
	in.spriteHeight = 56.f;
	return in;
}

} // namespace

TEST(SpriteClip, OnlyLivingThingsOrAHigherSettingClip)
{
	EXPECT_TRUE(ComputeSpriteClips(true, 1));
	EXPECT_FALSE(ComputeSpriteClips(false, 1));
	EXPECT_TRUE(ComputeSpriteClips(false, 2));
	EXPECT_TRUE(ComputeSpriteClips(false, 3));
}

TEST(SpriteClip, AMonsterSunkIntoTheFloorIsLiftedOut)
{
	float z1 = 50.f, z2 = -6.f;   // [rc4l] its bottom 6 below a floor at 0
	ComputeSpriteClip(Monster(0.f), z1, z2);
	EXPECT_EQ(z2, -1.f);   // [rc4l] (z2 + 1) - floor, as GL measures it
	EXPECT_EQ(z1, 55.f);
	// [rc4l] Standing above the floor, nothing moves.
	z1 = 60.f; z2 = 4.f;
	ComputeSpriteClip(Monster(0.f), z1, z2);
	EXPECT_EQ(z2, 4.f);
}

TEST(SpriteClip, ASinkTooDeepToBeRealIsLeftAlone)
{
	float z1 = 50.f, z2 = -30.f;
	SpriteClipIn in = Monster(0.f);
	ComputeSpriteClip(in, z1, z2);   // [rc4l] 29 deep: more than 1.8 thresholds, so no lift
	EXPECT_EQ(z2, -30.f);
	z1 = 50.f; z2 = -15.f;
	ComputeSpriteClip(in, z1, z2);   // [rc4l] 14 deep: a living monster may sink that far
	EXPECT_EQ(z2, -1.f);
	in.monster = false;
	z1 = 50.f; z2 = -15.f;
	ComputeSpriteClip(in, z1, z2);   // [rc4l] anything else may not
	EXPECT_EQ(z2, -15.f);
}

TEST(SpriteClip, TheSmarterClipScalesTheLiftAndClearsTheCeiling)
{
	SpriteClipIn in = Monster(0.f);
	in.living = false;
	in.monster = false;
	in.corpse = true;
	in.spriteClip = 3;
	float z1 = 50.f, z2 = -6.f;
	ComputeSpriteClip(in, z1, z2);
	// [rc4l] A corpse's lift is scaled by the clip factor over its height, at least half.
	EXPECT_EQ(z2, -6.f + 5.f * 0.5f);
	// [rc4l] Resting on the floor, a sprite poking through the ceiling is pulled down.
	in.corpse = false;
	in.top = 40.f;
	z1 = 45.f; z2 = 2.f;
	ComputeSpriteClip(in, z1, z2);
	EXPECT_EQ(z1, 45.f - 2.f);   // [rc4l] 4 through, scaled to at least half
	EXPECT_EQ(z2, 2.f - 2.f);
	// [rc4l] Not below it, and not by more than the threshold unless it is a monster that may.
	in.top = 60.f;
	z1 = 45.f; z2 = 2.f;
	ComputeSpriteClip(in, z1, z2);
	EXPECT_EQ(z1, 45.f);
	in.top = 20.f;
	z1 = 45.f; z2 = 2.f;
	ComputeSpriteClip(in, z1, z2);
	EXPECT_EQ(z1, 45.f);
	in.monster = true;
	in.top = 34.f;   // [rc4l] exactly the threshold
	z1 = 45.f; z2 = 2.f;
	ComputeSpriteClip(in, z1, z2);
	EXPECT_LT(z1, 45.f);
	// [rc4l] A sprite shorter than its overlap takes the whole shift.
	in.spriteHeight = 2.f;
	in.top = 40.f;
	z1 = 45.f; z2 = 2.f;
	ComputeSpriteClip(in, z1, z2);
	EXPECT_EQ(z1, 41.f);
}

TEST(SpriteClipIdle, AnythingAboveTheFloorIsLeftAloneAndAnythingInItIsNot)
{
	EXPECT_TRUE(ComputeSpriteClipIdle(Monster(0.f), 50.f, -1.f));
	EXPECT_TRUE(ComputeSpriteClipIdle(Monster(0.f), 50.f, 20.f));
	EXPECT_FALSE(ComputeSpriteClipIdle(Monster(0.f), 50.f, -1.5f));
	// [Doruk-Aksoy] Without the smarter clip the ceiling is never measured, however far through it the top goes.
	EXPECT_TRUE(ComputeSpriteClipIdle(Monster(0.f), 5000.f, 0.f));
}

TEST(SpriteClipIdle, TheSmarterClipAlsoWantsTheTopUnderTheCeiling)
{
	SpriteClipIn in = Monster(0.f);
	in.living = false;
	in.spriteClip = 3;
	in.top = 100.f;
	EXPECT_TRUE(ComputeSpriteClipIdle(in, 101.f, 0.f));
	EXPECT_FALSE(ComputeSpriteClipIdle(in, 101.5f, 0.f));
}

TEST(SpriteClipIdle, WhenIdleTheClipMovesNothing)
{
	// [Doruk-Aksoy] Every edge pair it calls idle, under every clip setting and kind of actor, comes back from ComputeSpriteClip as it went in.
	for (int clip = 1; clip <= 3; clip++)
		for (int kind = 0; kind < 4; kind++)
			for (float z2 = -40.f; z2 <= 40.f; z2 += 0.5f)
				for (float z1 = z2 + 10.f; z1 <= 160.f; z1 += 7.5f)
				{
					SpriteClipIn in = Monster(0.f);
					in.spriteClip = clip;
					in.living = (kind & 1) != 0;
					in.corpse = (kind & 2) != 0;
					in.top = 100.f;
					if (!ComputeSpriteClipIdle(in, z1, z2)) continue;
					float a = z1, b = z2;
					ComputeSpriteClip(in, a, b);
					EXPECT_EQ(z1, a);
					EXPECT_EQ(z2, b);
				}
}

// [Doruk-Aksoy] A corpse resting below its frame's bottom offset was left to the CPU each frame though its lift never changed within the tic.
TEST(SpriteClip, AnActorIsSteadyOnlyWithItsHeightFloorAndCeilingKept)
{
	const TicEnd rest = { 64, 0, 256 };
	EXPECT_TRUE(ComputeSteadyAcrossTic(rest, rest));
	EXPECT_FALSE(ComputeSteadyAcrossTic(rest, { 65, 0, 256 }));
	EXPECT_FALSE(ComputeSteadyAcrossTic(rest, { 64, 8, 256 }));
	EXPECT_FALSE(ComputeSteadyAcrossTic(rest, { 64, 0, 248 }));
}
