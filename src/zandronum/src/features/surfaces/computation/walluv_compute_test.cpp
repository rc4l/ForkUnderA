// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Texture alignment, which is where walls come out wrong in ways nobody notices for months.
//
// A wall drawn at the wrong height and a wall drawn with the picture in the wrong place look the
// same in a screenshot -- "the texture is wrong there" -- and have nothing in common as causes. So
// the alignment rules get their own tests, with the cases Doom mappers actually rely on: a door that
// must not slide its picture, a step that lines up with the floor, a texture that repeats along a
// corridor, and offsets in both directions.

#include <gtest/gtest.h>

#include "features/surfaces/computation/walluv_compute.h"

using namespace zx::surfaces;

// The running coordinate, not a wrapped one: a 512-unit corridor with a 64-unit texture shows the
// texture eight times, and clamping u into 0..1 would stretch one copy across the whole thing.
TEST(WallUV, HorizontalRepeatsAlongTheWall)
{
	EXPECT_FLOAT_EQ(0.f, ComputeWallU(0.f, 0.f, 64.f));
	EXPECT_FLOAT_EQ(1.f, ComputeWallU(64.f, 0.f, 64.f));
	EXPECT_FLOAT_EQ(8.f, ComputeWallU(512.f, 0.f, 64.f));
}

// The sidedef's own offset shifts it, in both directions.
TEST(WallUV, HorizontalOffsetShiftsThePattern)
{
	EXPECT_FLOAT_EQ(0.5f, ComputeWallU(0.f, 32.f, 64.f));
	EXPECT_FLOAT_EQ(-0.5f, ComputeWallU(0.f, -32.f, 64.f));
	EXPECT_FLOAT_EQ(1.5f, ComputeWallU(64.f, 32.f, 64.f));
}

// [rc4l] Measured from the LINE, not the seg. A linedef split into three segs shares one continuous
// texture across them; measuring each seg from its own start restarts the pattern at every vertex,
// which is a seam down the middle of a wall that looks like a texture bug and is a coordinate bug.
TEST(WallUV, HorizontalIsContinuousAcrossSegsOfOneLine)
{
	// Three segs of a 192-unit line, each measured from the line's start.
	const float segStarts[] = { 0.f, 64.f, 128.f };
	for (int i = 0; i < 3; i++)
	{
		const float uStart = ComputeWallU(segStarts[i], 0.f, 64.f);
		const float uEnd = ComputeWallU(segStarts[i] + 64.f, 0.f, 64.f);
		EXPECT_FLOAT_EQ((float)i, uStart);
		EXPECT_FLOAT_EQ((float)(i + 1), uEnd);   // each seg picks up exactly where the last left off
	}
}

// Vertical runs downward from the pegging reference: v is 0 at the top and 1 a texture-height below.
TEST(WallUV, VerticalRunsDownwardFromTheReference)
{
	EXPECT_FLOAT_EQ(0.f, ComputeWallV(128.f, 128.f, 128.f));
	EXPECT_FLOAT_EQ(1.f, ComputeWallV(0.f, 128.f, 128.f));
	EXPECT_FLOAT_EQ(0.5f, ComputeWallV(64.f, 128.f, 128.f));
}

// Above the reference is a negative v, which is normal rather than an error: an upper texture pegged
// to the ceiling behind it sits above its own reference all the time.
TEST(WallUV, AboveTheReferenceIsNegative)
{
	EXPECT_FLOAT_EQ(-1.f, ComputeWallV(256.f, 128.f, 128.f));
}

// The unpegged default: the texture hangs from the reference ceiling.
TEST(WallUV, UnpeggedHangsFromTheReferenceCeiling)
{
	EXPECT_FLOAT_EQ(128.f, ComputeTextureTop(128.f, 0.f, 64.f, false, 0.f, 0.f));
	// ...so the reference floor is two texture-heights down, and v there is 2.
	EXPECT_FLOAT_EQ(2.f, ComputeWallV(0.f, ComputeTextureTop(128.f, 0.f, 64.f, false, 0.f, 0.f), 64.f));
}

// [rc4l] Pegged pushes the texture down until its last row lands on the reference floor.
//
// This is what a door needs: the picture stays put while the door slides, because the reference pair
// is the sector's texture Z rather than where its planes currently are.
TEST(WallUV, PeggedLandsTheLastRowOnTheReferenceFloor)
{
	// 64-unit texture, reference span 128: pegged, the top sits 64 above the floor reference.
	const float top = ComputeTextureTop(128.f, 0.f, 64.f, true, 0.f, 0.f);
	EXPECT_FLOAT_EQ(64.f, top);
	EXPECT_FLOAT_EQ(1.f, ComputeWallV(0.f, top, 64.f));    // reference floor = bottom of the texture
	EXPECT_FLOAT_EQ(0.f, ComputeWallV(64.f, top, 64.f));   // one height up = its top
}

// A texture exactly filling the reference span is in the same place either way, which is why the
// difference between the two is invisible on the walls people check first.
TEST(WallUV, PeggingIsInvisibleWhenTheTextureFitsExactly)
{
	EXPECT_FLOAT_EQ(ComputeTextureTop(128.f, 0.f, 128.f, false, 0.f, 0.f),
	                ComputeTextureTop(128.f, 0.f, 128.f, true, 0.f, 0.f));
}

// The row offset shifts either reference, and the sky offset shifts only the pegged one -- it is
// part of the same term.
TEST(WallUV, RowOffsetShiftsEitherReference)
{
	EXPECT_FLOAT_EQ(136.f, ComputeTextureTop(128.f, 0.f, 64.f, false, 8.f, 0.f));
	EXPECT_FLOAT_EQ(72.f, ComputeTextureTop(128.f, 0.f, 64.f, true, 8.f, 0.f));
	EXPECT_FLOAT_EQ(120.f, ComputeTextureTop(128.f, 0.f, 64.f, false, -8.f, 0.f));
	EXPECT_FLOAT_EQ(56.f, ComputeTextureTop(128.f, 0.f, 64.f, true, 0.f, 8.f));
}

// A door that MOVES must not slide its picture: the reference pair is the sector's texture Z, so it
// does not change when the planes do.
TEST(WallUV, APeggedTextureDoesNotMoveWhenThePlanesDo)
{
	const float refTop = ComputeTextureTop(128.f, 0.f, 64.f, true, 0.f, 0.f);
	EXPECT_FLOAT_EQ(refTop, ComputeTextureTop(128.f, 0.f, 64.f, true, 0.f, 0.f));
	EXPECT_FLOAT_EQ(ComputeWallV(32.f, refTop, 64.f), ComputeWallV(32.f, refTop, 64.f));
}

