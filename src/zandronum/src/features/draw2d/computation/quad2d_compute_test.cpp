// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/computation/quad2d_compute.h"

#include <gtest/gtest.h>
#include <string.h>

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

// [rc4l] Every field the texture does not give is zero: a HUD quad that carried a weapon's fixed colormap or fuzz from the stack drew black.
TEST(Quad2D, ATextureRecordIsItsQuadAndNothingElse)
{
	TextureQuadOut q;
	ComputeTextureQuad(Patch(), q);
	q.r = 51; q.a = 102;
	zx::hwrender::Quad2D garbage;
	memset(&garbage, 0x5a, sizeof(garbage));
	zx::hwrender::Quad2D d = garbage;
	d = ComputeTextureQuadRecord(q);
	EXPECT_FLOAT_EQ(d.x, 84.f); EXPECT_FLOAT_EQ(d.y, 42.f); EXPECT_FLOAT_EQ(d.w, 128.f); EXPECT_FLOAT_EQ(d.h, 64.f);
	EXPECT_FLOAT_EQ(d.u2, 1.f); EXPECT_FLOAT_EQ(d.v2, 1.f);
	EXPECT_FLOAT_EQ(d.r, 0.2f); EXPECT_FLOAT_EQ(d.g, 1.f); EXPECT_FLOAT_EQ(d.a, 0.4f);
	EXPECT_EQ(d.fixedColormap, 0);
	EXPECT_EQ(d.fuzz, 0);
	EXPECT_EQ(d.tint.strength, 0.f);
	EXPECT_EQ(d.kind, kQuadRect);
	EXPECT_EQ(d.blend, kBlend2DAlpha);
	EXPECT_EQ(d.material, (const void *)NULL);
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

TEST(Quad2D, APolygonsTextureRunsFromItsOriginOverItsScaledSize)
{
	using namespace zx::draw2d;
	PolyTexIn in = { 10.0, 20.0, 1.0, 2.0, 0u, 64, 32, false };
	const float points[4] = { 10.5f, 20.5f, 74.5f, 84.5f };
	float uv[4];
	ComputePolyTexCoords(in, points, 2, uv);
	EXPECT_FLOAT_EQ(0.f, uv[0]);
	EXPECT_FLOAT_EQ(0.f, uv[1]);
	EXPECT_FLOAT_EQ(1.f, uv[2]);
	EXPECT_FLOAT_EQ(1.f, uv[3]);
	// [rc4l] A camera texture runs upside down.
	in.canvas = true;
	ComputePolyTexCoords(in, points, 2, uv);
	EXPECT_FLOAT_EQ(-1.f, uv[3]);
}

TEST(Quad2D, APolygonsTextureTurnsWithIt)
{
	using namespace zx::draw2d;
	// [rc4l] A quarter turn takes the u axis onto v.
	const PolyTexIn in = { 0.0, 0.0, 1.0, 1.0, 0x40000000u, 64, 64, false };
	const float points[2] = { 64.5f, 0.5f };
	float uv[2];
	ComputePolyTexCoords(in, points, 1, uv);
	EXPECT_NEAR(0.f, uv[0], 1e-6f);
	EXPECT_NEAR(1.f, uv[1], 1e-6f);
}

TEST(Quad2D, APolygonIsAFanOfTrianglesFromItsFirstPoint)
{
	using namespace zx::draw2d;
	zx::hwrender::Quad2D base = ComputeFillQuad(0, 0, 0, 0, 0xffffffu, 1.f);
	base.material = &base;
	const float points[8] = { 0, 0, 10, 0, 10, 10, 0, 10 };
	const float uv[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
	std::vector<zx::hwrender::Quad2D> tris;
	ComputePolyTriangles(base, points, uv, 4, tris);
	ASSERT_EQ(2u, tris.size());
	EXPECT_EQ(kQuadTri, tris[0].kind);
	EXPECT_EQ(&base, tris[0].material);
	EXPECT_EQ(10.f, tris[0].lx2);
	EXPECT_EQ(10.f, tris[0].y3);
	EXPECT_EQ(1.f, tris[1].u2);
	EXPECT_EQ(0.f, tris[1].x3);
	EXPECT_EQ(1.f, tris[1].v3);
	ComputePolyTriangles(base, points, uv, 2, tris);
	EXPECT_TRUE(tris.empty());
}

// [rc4l] A greyed menu row is its glyphs under MAKEARGB(96,48,0,0), which GL draws as a second pass and the backend dropped.
TEST(Quad2D, AnOverlayIsTheGlyphsCoverageInItsColour)
{
	zx::hwrender::Quad2D base = ComputeFillQuad(10.f, 20.f, 30.f, 40.f, 0xffffffu, 1.f);
	base.material = &base;
	base.u1 = 0.25f; base.v2 = 0.75f;
	base.translation = 7; base.fuzz = 2; base.fixedColormap = 3; base.texMode = kTexModulate; base.blend = kBlend2DAdditive;
	base.tint.strength = 0.5f;
	zx::hwrender::Quad2D over;
	ASSERT_TRUE(ComputeOverlayQuad(base, 0x60300000u, over));
	EXPECT_EQ(over.material, base.material);
	EXPECT_FLOAT_EQ(over.x, 10.f); EXPECT_FLOAT_EQ(over.w, 30.f); EXPECT_FLOAT_EQ(over.u1, 0.25f); EXPECT_FLOAT_EQ(over.v2, 0.75f);
	EXPECT_FLOAT_EQ(over.r, 0x30 / 255.f); EXPECT_FLOAT_EQ(over.g, 0.f); EXPECT_FLOAT_EQ(over.b, 0.f);
	EXPECT_FLOAT_EQ(over.a, 0x60 / 255.f);
	EXPECT_EQ(over.texMode, kTexMask);
	EXPECT_EQ(over.blend, kBlend2DAlpha);
	EXPECT_EQ(over.translation, 0);
	EXPECT_EQ(over.fuzz, 0);
	EXPECT_EQ(over.fixedColormap, 0);
	EXPECT_FLOAT_EQ(over.tint.strength, 0.f);
}

TEST(Quad2D, NoOverlayAddsNoPass)
{
	const zx::hwrender::Quad2D base = ComputeFillQuad(0.f, 0.f, 1.f, 1.f, 0xffffffu, 1.f);
	zx::hwrender::Quad2D over = base;
	EXPECT_FALSE(ComputeOverlayQuad(base, 0u, over));
}

TEST(Quad2D, TheSameListKeepsItsVertices)
{
	EXPECT_FALSE(ComputeRebuild2DVertices(7u, 7u, 40, 40));
}

TEST(Quad2D, AClearedListIsBuiltAgain)
{
	EXPECT_TRUE(ComputeRebuild2DVertices(8u, 7u, 40, 40));
}

// [rc4l] A wipe step recorded its console again under the same generation, and the pass drew those quads past the end of the vertices, which a browser's WebGPU refused for the whole frame.
TEST(Quad2D, QuadsRecordedAfterTheBuildAreBuiltToo)
{
	EXPECT_TRUE(ComputeRebuild2DVertices(7u, 7u, 912, 456));
	EXPECT_TRUE(ComputeRebuild2DVertices(7u, 7u, 10, 40));
}
