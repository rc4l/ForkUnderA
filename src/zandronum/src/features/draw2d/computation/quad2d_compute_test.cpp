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
