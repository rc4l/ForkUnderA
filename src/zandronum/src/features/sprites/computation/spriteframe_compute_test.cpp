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

TEST(SpriteFrame, SpriteFlipPicksTheRotationFromTheOtherWayRound)
{
	bool mirror = true;
	// [Doruk-Aksoy] UZDoom's flipagain: 90 degrees one way reads as 90 the other, so slot 13 becomes slot 5.
	EXPECT_EQ(ComputeSpriteRotation(0x40000000u, true, 0, mirror, false), 13);
	EXPECT_EQ(ComputeSpriteRotation(0x40000000u, true, 0, mirror, true), 5);
	EXPECT_FALSE(mirror);
	EXPECT_EQ(ComputeSpriteRotation(0x40000000u, false, 1u << 4, mirror, true), 4);
	EXPECT_TRUE(mirror);
	// [Doruk-Aksoy] Straight on is the same either way.
	EXPECT_EQ(ComputeSpriteRotation(0u, true, 0, mirror, true), ComputeSpriteRotation(0u, true, 0, mirror, false));
}

TEST(SpriteFrame, XFlipAndSpriteFlipEachTurnTheMirrorOver)
{
	EXPECT_FALSE(ComputeSpriteDrawnMirror(false, false, false));
	EXPECT_TRUE(ComputeSpriteDrawnMirror(true, false, false));
	EXPECT_TRUE(ComputeSpriteDrawnMirror(false, true, false));
	EXPECT_TRUE(ComputeSpriteDrawnMirror(false, false, true));
	EXPECT_FALSE(ComputeSpriteDrawnMirror(false, true, true));
	EXPECT_FALSE(ComputeSpriteDrawnMirror(true, true, false));
	EXPECT_TRUE(ComputeSpriteDrawnMirror(true, true, true));
}

TEST(SpriteFrame, YFlipSwapsTopAndBottom)
{
	float vt = 0.25f, vb = 0.75f;
	ComputeSpriteVFlip(false, vt, vb);
	EXPECT_EQ(vt, 0.25f);
	EXPECT_EQ(vb, 0.75f);
	ComputeSpriteVFlip(true, vt, vb);
	EXPECT_EQ(vt, 0.75f);
	EXPECT_EQ(vb, 0.25f);
}
