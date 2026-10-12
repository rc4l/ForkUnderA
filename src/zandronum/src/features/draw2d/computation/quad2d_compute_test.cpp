// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/computation/quad2d_compute.h"

#include <gtest/gtest.h>

using namespace zx::draw2d;

namespace {

// [rc4l] A 64x32 patch drawn at (100, 50) at twice its size, offset 8 left and 4 up, its texture filling 0..1.
TextureQuadIn Patch()
{
	TextureQuadIn in = {};
	in.x = 100; in.y = 50; in.left = 8; in.top = 4;
	in.destWidth = 128; in.destHeight = 64; in.texWidth = 64; in.texHeight = 32;
	in.windowLeft = 0; in.windowRight = 64;
	in.ul = 0.f; in.vt = 0.f; in.ur = 1.f; in.vb = 1.f;
	in.alpha = 1.f;
	return in;
}

} // namespace

TEST(Quad2D, TheStyleDecidesTheTextureMode)
{
	EXPECT_EQ(ComputeTextureMode(0, false), kTexModulate);
	EXPECT_EQ(ComputeTextureMode(0, true), kTexOpaque);
	EXPECT_EQ(ComputeTextureMode(kStyleRedIsAlpha | kStyleColorIsFixed, false), kTexRedToAlpha);
	EXPECT_EQ(ComputeTextureMode(kStyleColorIsFixed, true), kTexMask);
	EXPECT_EQ(ComputeTextureMode(kStyleInvertSource, false), kTexInverse);
}

TEST(Quad2D, OnlyAStyleAddingOntoTheScreenWholeBlendsAdditively)
{
	EXPECT_EQ(ComputeQuadBlend(kStyleOpAdd, kStyleAlphaOne), kBlend2DAdditive);   // [rc4l] STYLE_Add
	EXPECT_EQ(ComputeQuadBlend(kStyleOpAdd, 3), kBlend2DAlpha);                   // [rc4l] STYLE_Translucent's InvSrc
	EXPECT_EQ(ComputeQuadBlend(2, kStyleAlphaOne), kBlend2DAlpha);                // [rc4l] a subtraction is no addition
}

TEST(Quad2D, APatchLandsAtItsOffsetScaledToItsSize)
{
	TextureQuadOut q;
	ComputeTextureQuad(Patch(), q);
	EXPECT_FLOAT_EQ(q.x, 84.f);    // [rc4l] 100 less 8 doubled
	EXPECT_FLOAT_EQ(q.y, 42.f);
	EXPECT_FLOAT_EQ(q.w, 128.f);
	EXPECT_FLOAT_EQ(q.x2, 212.f);
	EXPECT_FLOAT_EQ(q.y2, 106.f);
	EXPECT_FLOAT_EQ(q.u1, 0.f);
	EXPECT_FLOAT_EQ(q.u2, 1.f);
	EXPECT_EQ(q.r, 255);
	EXPECT_EQ(q.a, 255);
	EXPECT_EQ(q.colorOverlay, 0u);
}

TEST(Quad2D, AWindowCropsTheQuadAndItsTexture)
{
	TextureQuadIn in = Patch();
	in.windowLeft = 16; in.windowRight = 48;   // [rc4l] the middle half
	TextureQuadOut q;
	ComputeTextureQuad(in, q);
	EXPECT_FLOAT_EQ(q.x, 116.f);
	EXPECT_FLOAT_EQ(q.w, 64.f);
	EXPECT_FLOAT_EQ(q.u1, 0.25f);
	EXPECT_FLOAT_EQ(q.u2, 0.75f);
	in.windowLeft = 0; in.windowRight = 32;    // [rc4l] cropped on the right only
	ComputeTextureQuad(in, q);
	EXPECT_FLOAT_EQ(q.x, 84.f);
	EXPECT_FLOAT_EQ(q.u2, 0.5f);
}

TEST(Quad2D, FlippedAndCanvasTexturesTurnTheirCoordinates)
{
	TextureQuadIn in = Patch();
	in.flipX = true;
	TextureQuadOut q;
	ComputeTextureQuad(in, q);
	EXPECT_FLOAT_EQ(q.u1, 1.f);
	EXPECT_FLOAT_EQ(q.u2, 0.f);
	in.flipX = false;
	in.canvas = true;   // [rc4l] drawn whole and upside down, whatever the material says
	in.ul = 0.2f;
	ComputeTextureQuad(in, q);
	EXPECT_FLOAT_EQ(q.u1, 0.f);
	EXPECT_FLOAT_EQ(q.v1, 1.f);
	EXPECT_FLOAT_EQ(q.v2, 0.f);
}

TEST(Quad2D, ABlackOverlayDarkensAndAnyOtherIsKept)
{
	TextureQuadIn in = Patch();
	in.colorOverlay = 0x80000000u;   // [rc4l] half black
	in.alpha = 0.5f;
	TextureQuadOut q;
	ComputeTextureQuad(in, q);
	EXPECT_EQ(q.r, 127);
	EXPECT_EQ(q.colorOverlay, 0u);
	EXPECT_EQ(q.a, 127);
	in.colorOverlay = 0x80ff0000u;   // [rc4l] half red, drawn over the quad
	ComputeTextureQuad(in, q);
	EXPECT_EQ(q.r, 255);
	EXPECT_EQ(q.colorOverlay, 0x80ff0000u);
}

TEST(Quad2D, AFixedColourStyleTakesTheFillColour)
{
	TextureQuadIn in = Patch();
	in.colorIsFixed = true;
	in.fillColor = 0xff102030u;
	TextureQuadOut q;
	ComputeTextureQuad(in, q);
	EXPECT_EQ(q.r, 0x10);
	EXPECT_EQ(q.g, 0x20);
	EXPECT_EQ(q.b, 0x30);
}

TEST(ComputeDrawColorDirect, AColourOrNoPaletteEntryIsDrawnDirect)
{
	// [rc4l] The player setup menu clears to black with palcolor -1 and colour 0, which read palette entry -1 when only GL knew the rule.
	EXPECT_TRUE(zx::draw2d::ComputeDrawColorDirect(-1, 0));
	EXPECT_TRUE(zx::draw2d::ComputeDrawColorDirect(4, 0xff102030u));
	EXPECT_FALSE(zx::draw2d::ComputeDrawColorDirect(4, 0));
}

TEST(Quad2D, AFillIsAnUntexturedRectangleInItsColour)
{
	const zx::hwrender::Quad2D q = ComputeFillQuad(2.f, 3.f, 40.f, 50.f, 0xff8000u, 0.5f);
	EXPECT_EQ(nullptr, q.material);
	EXPECT_FLOAT_EQ(40.f, q.w); EXPECT_FLOAT_EQ(50.f, q.h);
	EXPECT_FLOAT_EQ(1.f, q.r); EXPECT_FLOAT_EQ(128.f / 255.f, q.g); EXPECT_FLOAT_EQ(0.f, q.b); EXPECT_FLOAT_EQ(0.5f, q.a);
	EXPECT_EQ(kBlend2DAlpha, q.blend); EXPECT_EQ(kQuadRect, q.kind);
	EXPECT_EQ(0, q.clipR - q.clipL);
}

TEST(Quad2D, ALineCarriesItsFarEndAndAPixelIsOneSquare)
{
	const zx::hwrender::Quad2D l = ComputeLineQuad(1, 2, 30, 40, 0x00ff00u);
	EXPECT_EQ(kQuadLine, l.kind);
	EXPECT_FLOAT_EQ(30.f, l.lx2); EXPECT_FLOAT_EQ(40.f, l.ly2);
	EXPECT_FLOAT_EQ(1.f, l.g);
	const zx::hwrender::Quad2D p = ComputePixelQuad(5, 6, 0);
	EXPECT_FLOAT_EQ(1.f, p.w); EXPECT_FLOAT_EQ(1.f, p.h); EXPECT_FLOAT_EQ(0.f, p.u2);
}

TEST(Quad2D, TheScreenTintCoversTheScreenMultipliedOrOver)
{
	const zx::hwrender::Quad2D m = ComputeScreenFillQuad(640.f, 400.f, 0.5f, 0.25f, 1.f, 0.75f, true);
	EXPECT_FLOAT_EQ(640.f, m.w); EXPECT_FLOAT_EQ(400.f, m.h);
	EXPECT_FLOAT_EQ(0.25f, m.g); EXPECT_FLOAT_EQ(0.75f, m.a);
	EXPECT_EQ(kBlend2DMultiply, m.blend);
	EXPECT_EQ(kBlend2DAlpha, ComputeScreenFillQuad(1.f, 1.f, 0.f, 0.f, 0.f, 1.f, false).blend);
}
