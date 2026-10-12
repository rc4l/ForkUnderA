// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/pspritequad_compute.h"

#include <gtest/gtest.h>

using namespace zx::sprites;

namespace {

// [rc4l] A 64 by 32 frame at the origin on a 320x200 4:3 view filling the screen.
PSpriteQuadIn Plain()
{
	PSpriteQuadIn in = {};
	in.rectWidth = 64; in.rectHeight = 32;
	in.scaleX = in.scaleY = 1;
	in.viewWidth = 320; in.viewHeight = 200;
	in.widescreenRatio = 4.f / 3.f;
	in.screenWidth = 320; in.screenHeight = 200;
	return in;
}

} // namespace

TEST(PSpriteQuad, AFrameLandsAtItsOffsetOnA43View)
{
	PSpriteQuad q;
	ASSERT_TRUE(ComputePSpriteQuad(Plain(), q));
	EXPECT_FLOAT_EQ(q.x1, 0.f); EXPECT_FLOAT_EQ(q.x2, 64.f);
	EXPECT_FLOAT_EQ(q.y1, 0.f); EXPECT_FLOAT_EQ(q.y2, 33.f);   // [rc4l] DrawPSprite's extra row
}

TEST(PSpriteQuad, AWideScreenNarrowsTheWeaponAndATallOneKeepsItWhole)
{
	PSpriteQuadIn in = Plain();
	in.viewWidth = 640; in.widescreenRatio = 16.f / 9.f;
	PSpriteQuad q;
	ASSERT_TRUE(ComputePSpriteQuad(in, q));
	EXPECT_FLOAT_EQ(q.x2 - q.x1, 64.f * 1.5f);
	in.tallerThanWide = true;
	ASSERT_TRUE(ComputePSpriteQuad(in, q));
	EXPECT_FLOAT_EQ(q.x2 - q.x1, 128.f);
}

TEST(PSpriteQuad, TheWindowYAdjustAndScaleMoveIt)
{
	PSpriteQuadIn in = Plain();
	in.windowX = 8; in.windowY = 4; in.yAdjust = 10; in.scaleY = 2;
	PSpriteQuad q;
	ASSERT_TRUE(ComputePSpriteQuad(in, q));
	EXPECT_FLOAT_EQ(q.x1, 8.f);
	EXPECT_FLOAT_EQ(q.y1, 14.f);
	EXPECT_FLOAT_EQ(q.y2, 14.f + 64.f + 1.f);
}

TEST(PSpriteQuad, OffEitherSideIsNotDrawn)
{
	PSpriteQuadIn in = Plain();
	PSpriteQuad q;
	in.sx = 1000;
	EXPECT_FALSE(ComputePSpriteQuad(in, q));
	in.sx = -1000;
	EXPECT_FALSE(ComputePSpriteQuad(in, q));
}

TEST(PSpriteQuad, AMirroredLayerReflectsAboutTheViewsMiddle)
{
	PSpriteQuadIn in = Plain();
	in.mirror = true;
	PSpriteQuad q;
	ASSERT_TRUE(ComputePSpriteQuad(in, q));
	EXPECT_FLOAT_EQ(q.x1, 256.f); EXPECT_FLOAT_EQ(q.x2, 320.f);
}

TEST(PSpriteQuad, WeaponLight)
{
	EXPECT_EQ(ComputeSoftWeaponLight(255), 255);
	EXPECT_EQ(ComputeSoftWeaponLight(0), 0);
	EXPECT_EQ(ComputeSoftWeaponLight(100), 162);
	EXPECT_EQ(ComputeBrightenedWeaponLight(90, true, 3), 145);
	EXPECT_EQ(ComputeBrightenedWeaponLight(90, true, 8), 90);
	EXPECT_EQ(ComputeBrightenedWeaponLight(90, false, 3), 90);
}
