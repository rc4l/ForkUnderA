// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/spritequad_compute.h"

#include <gtest/gtest.h>
#include <math.h>

using namespace zx::sprites;

TEST(SpriteQuad, AMirroredFrameFlipsAboutTheOriginAndSwapsItsU)
{
	const SpriteRect frame = { -10.f, 40.f, 30.f, 50.f };
	float ul = 0.f, ur = 0.f;
	SpriteRect r = ComputeSpriteRect(frame, 0.1f, 0.9f, false, 2.f, 0.5f, ul, ur);
	EXPECT_EQ(r.left, -20.f); EXPECT_EQ(r.width, 60.f);
	EXPECT_EQ(r.top, 20.f);   EXPECT_EQ(r.height, 25.f);
	EXPECT_EQ(ul, 0.9f);      EXPECT_EQ(ur, 0.1f);   // [rc4l] unmirrored, GL draws the left side with the right u
	r = ComputeSpriteRect(frame, 0.1f, 0.9f, true, 1.f, 1.f, ul, ur);
	EXPECT_EQ(r.left, -20.f);   // [rc4l] -width - left
	EXPECT_EQ(ul, 0.1f);      EXPECT_EQ(ur, 0.9f);
}

TEST(SpriteQuad, AFaceSpriteSpansItsWidthAcrossTheViewVector)
{
	// [rc4l] A 32-wide frame centred on its origin, 56 tall with its top 56 above it, seen looking north.
	const SpriteRect r = { -16.f, 56.f, 32.f, 56.f };
	const SpriteEdges e = ComputeSpriteEdges(100.f, 200.f, 0.f, r, false, 0.f, 1.f);
	EXPECT_EQ(e.z1, -56.f);   // [rc4l] z1 = z - top, as GL writes it
	EXPECT_EQ(e.z2, -112.f);
	EXPECT_EQ(e.x1, 100.f + 16.f);
	EXPECT_EQ(e.x2, 100.f - 16.f);
	EXPECT_EQ(e.y1, 200.f);
	EXPECT_EQ(e.y2, 200.f);
}

TEST(SpriteQuad, AWallSpriteTurnsTheOtherWay)
{
	const SpriteRect r = { -16.f, 0.f, 32.f, 10.f };
	const SpriteEdges e = ComputeSpriteEdges(0.f, 0.f, 0.f, r, true, 1.f, 0.f);
	EXPECT_EQ(e.x1, 0.f);
	EXPECT_EQ(e.y1, 16.f);
	EXPECT_EQ(e.y2, -16.f);
}

TEST(SpriteQuad, APlainBillboardKeepsItsEdges)
{
	const SpriteEdges e = { 1.f, 2.f, 3.f, 4.f, 10.f, 5.f };
	float c[4][3];
	ComputeSpriteCorners(e, false, false, 0.f, 0.f, 0.f, c);
	EXPECT_EQ(c[0][0], 1.f); EXPECT_EQ(c[0][1], 10.f); EXPECT_EQ(c[0][2], 2.f);
	EXPECT_EQ(c[1][0], 3.f); EXPECT_EQ(c[1][1], 10.f); EXPECT_EQ(c[1][2], 4.f);
	EXPECT_EQ(c[2][0], 1.f); EXPECT_EQ(c[2][1], 5.f);  EXPECT_EQ(c[2][2], 2.f);
	EXPECT_EQ(c[3][0], 3.f); EXPECT_EQ(c[3][1], 5.f);  EXPECT_EQ(c[3][2], 4.f);
}

TEST(SpriteQuad, AnXYBillboardTiltsWithThePitchAboutItsCentre)
{
	const SpriteEdges e = { -8.f, 0.f, 8.f, 0.f, 20.f, 0.f };
	float c[4][3];
	ComputeSpriteCorners(e, true, false, 90.f, 0.f, 0.f, c);
	// [rc4l] No pitch: nothing moves.
	EXPECT_NEAR(c[0][1], 20.f, 1e-4f);
	EXPECT_NEAR(c[3][1], 0.f, 1e-4f);
	ComputeSpriteCorners(e, true, false, 180.f, 30.f, 0.f, c);
	// [rc4l] Pitched, the top and bottom swing towards and away from the eye, and the centre stays.
	EXPECT_NEAR((c[0][1] + c[3][1]) * 0.5f, 10.f, 1e-4f);
	EXPECT_GT(fabsf(c[0][2] - c[2][2]), 1.f);
}

TEST(SpriteQuad, ARolledSpriteTurnsAboutTheSightLine)
{
	const SpriteEdges e = { -8.f, 0.f, 8.f, 0.f, 8.f, -8.f };
	float c[4][3];
	ComputeSpriteCorners(e, false, true, 180.f, 0.f, 90.f, c);
	// [rc4l] A quarter turn takes the top-left corner to where a side was.
	EXPECT_NEAR(fabsf(c[0][0]) + fabsf(c[0][1]), 16.f, 1e-3f);
	EXPECT_NEAR(c[0][2], 0.f, 1e-3f);
}
