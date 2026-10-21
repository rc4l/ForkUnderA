// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/spriteframe_compute.h"

#include <gtest/gtest.h>

using namespace zx::sprites;

TEST(SpriteFrame, EightRotationsCentredOnEachDirection)
{
	bool mirror = true;
	// [rc4l] The angle is moved on by nine half-steps of 22.5 degrees and its top four bits pick one of sixteen slots.
	EXPECT_EQ(ComputeSpriteRotation(0x00000000u, true, 0, mirror), 9);
	EXPECT_FALSE(mirror);
	EXPECT_EQ(ComputeSpriteRotation(0x80000000u, true, 0, mirror), 1);
	EXPECT_EQ(ComputeSpriteRotation(0xC0000000u, true, 0, mirror), 5);
}

TEST(SpriteFrame, AnAsymmetricFrameIsOffsetBySixteenthOfATurn)
{
	bool mirror = false;
	// [rc4l] A quarter of a slot past a boundary: the symmetric frame is over it, the offset one not yet.
	EXPECT_EQ(ComputeSpriteRotation(0x84000000u, true, 0, mirror), 1);
	EXPECT_EQ(ComputeSpriteRotation(0x84000000u, false, 0, mirror), 0);
}

TEST(SpriteFrame, TheFlipMaskMirrorsItsRotations)
{
	bool mirror = false;
	const int rot = ComputeSpriteRotation(0x80000000u, true, 1u << 1, mirror);
	EXPECT_EQ(rot, 1);
	EXPECT_TRUE(mirror);
}

// [rc4l] A model captures through the engine and a player keeps the damage tint's per-actor state, so neither is derived on a lane.
TEST(SpriteFrame, ModelsAndPlayersAreDerivedOnTheGameThread)
{
	EXPECT_FALSE(ComputeSpriteOnGameThread(false, false));
	EXPECT_TRUE(ComputeSpriteOnGameThread(true, false));
	EXPECT_TRUE(ComputeSpriteOnGameThread(false, true));
}
