// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/materials/computation/materiallayout_compute.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

using namespace zx::materials;

namespace {

// [rc4l] A 16 by 32 sprite with its origin at (8, 30), drawn at its size and scaled two texels to a unit across.
const MaterialSource kSprite = { 16, 32, 8, 30, 16, 32, 2.f, 1.f };
const int kNoTrim[4] = { 0, 0, 0, 0 };

// [rc4l] An RGBA picture `w` by `h`, opaque where `opaque` lists texel indices.
std::vector<unsigned char> Picture(int w, int h, std::initializer_list<int> opaque)
{
	std::vector<unsigned char> p(w * h * 4, 0);
	for (int i : opaque) p[i * 4 + 3] = 255;
	return p;
}

} // namespace

TEST(MaterialLayout, AnUnframedMaterialIsTheTexture)
{
	const MaterialLayout l = ComputeMaterialLayout(kSprite, false, false, kNoTrim);
	EXPECT_EQ(16, l.width); EXPECT_EQ(32, l.height);
	EXPECT_EQ(8, l.leftOffset); EXPECT_EQ(30, l.topOffset);
	EXPECT_EQ(16, l.renderWidth); EXPECT_EQ(32, l.renderHeight);
	EXPECT_FLOAT_EQ(-4.f, l.spriteLeft); EXPECT_FLOAT_EQ(-30.f, l.spriteTop);
	EXPECT_FLOAT_EQ(8.f, l.spriteWidth); EXPECT_FLOAT_EQ(32.f, l.spriteHeight);
	EXPECT_EQ(0.f, l.spriteU[0]); EXPECT_EQ(1.f, l.spriteU[1]);
	EXPECT_EQ(0.f, l.spriteV[0]); EXPECT_EQ(1.f, l.spriteV[1]);
}

// [rc4l] The frame adds a texel each side, moves the origin one in, and grows the drawn size by the same share.
TEST(MaterialLayout, AFramedSpriteGrowsByATexelEachSide)
{
	const MaterialLayout l = ComputeMaterialLayout(kSprite, true, false, kNoTrim);
	EXPECT_EQ(18, l.width); EXPECT_EQ(34, l.height);
	EXPECT_EQ(9, l.leftOffset); EXPECT_EQ(31, l.topOffset);
	EXPECT_EQ(18, l.renderWidth); EXPECT_EQ(34, l.renderHeight);
	EXPECT_FLOAT_EQ(-4.5f, l.spriteLeft); EXPECT_FLOAT_EQ(-31.f, l.spriteTop);
	EXPECT_FLOAT_EQ(9.f, l.spriteWidth); EXPECT_FLOAT_EQ(34.f, l.spriteHeight);
	EXPECT_EQ(1.f, l.spriteU[1]);
}

// [rc4l] Trimming to columns 4-11 and rows 2-29 cuts the rectangle and the texture coordinates to just those texels.
TEST(MaterialLayout, ATrimmedSpriteDrawsOnlyItsOpaqueTexels)
{
	const int trim[4] = { 4, 2, 8, 28 };
	const MaterialLayout l = ComputeMaterialLayout(kSprite, true, true, trim);
	EXPECT_FLOAT_EQ(-4.5f + 2.f, l.spriteLeft);
	EXPECT_FLOAT_EQ(-31.f + 2.f, l.spriteTop);
	EXPECT_FLOAT_EQ(9.f - 4.f, l.spriteWidth);
	EXPECT_FLOAT_EQ(34.f - 4.f, l.spriteHeight);
	EXPECT_FLOAT_EQ(4.f / 18.f, l.spriteU[0]);
	EXPECT_FLOAT_EQ(2.f / 34.f, l.spriteV[0]);
	EXPECT_FLOAT_EQ(1.f - 4.f / 18.f, l.spriteU[1]);
	EXPECT_FLOAT_EQ(1.f - 2.f / 34.f, l.spriteV[1]);
}

TEST(TrimBorders, ASingleTexelIsNotTrimmed)
{
	const auto p = Picture(1, 1, { 0 });
	int r[4] = { -1, -1, -1, -1 };
	EXPECT_FALSE(ComputeTrimBorders(p.data(), 1, 1, r));
	EXPECT_EQ(-1, r[0]);
}

TEST(TrimBorders, AnEmptyPictureTrimsToItsCorner)
{
	const auto p = Picture(4, 4, {});
	int r[4];
	ASSERT_TRUE(ComputeTrimBorders(p.data(), 4, 4, r));
	EXPECT_EQ(0, r[0]); EXPECT_EQ(0, r[1]); EXPECT_EQ(1, r[2]); EXPECT_EQ(1, r[3]);
}

TEST(TrimBorders, AnOpaqueBlockIsCutOutOfItsBorder)
{
	// [rc4l] Texels (1,1), (2,1), (1,2) and (2,2) of a 4 by 4.
	const auto p = Picture(4, 4, { 5, 6, 9, 10 });
	int r[4];
	ASSERT_TRUE(ComputeTrimBorders(p.data(), 4, 4, r));
	EXPECT_EQ(1, r[0]); EXPECT_EQ(1, r[1]); EXPECT_EQ(2, r[2]); EXPECT_EQ(2, r[3]);
}

TEST(TrimBorders, OneOpaqueTexelTrimsToItself)
{
	const auto p = Picture(3, 3, { 4 });
	int r[4];
	ASSERT_TRUE(ComputeTrimBorders(p.data(), 3, 3, r));
	EXPECT_EQ(1, r[0]); EXPECT_EQ(1, r[1]); EXPECT_EQ(1, r[2]); EXPECT_EQ(1, r[3]);
}

TEST(TrimBorders, OpaqueEdgesKeepTheWholeWidth)
{
	const auto p = Picture(3, 2, { 0, 2 });
	int r[4];
	ASSERT_TRUE(ComputeTrimBorders(p.data(), 3, 2, r));
	EXPECT_EQ(0, r[0]); EXPECT_EQ(0, r[1]); EXPECT_EQ(3, r[2]); EXPECT_EQ(1, r[3]);
}

TEST(StoredDimension, ASideIsCappedAtTheDevicesLargest)
{
	EXPECT_EQ(100, ComputeStoredDimension(100, 0));
	EXPECT_EQ(64, ComputeStoredDimension(100, 64));
	EXPECT_EQ(32, ComputeStoredDimension(32, 64));
}

// [rc4l] A warp is first, then a camera texture, then a hardware shader (12 and up here), else the material is plain.
TEST(MaterialDraw, TheFirstThatAppliesWins)
{
	EXPECT_EQ(kDrawWarp, ComputeMaterialDraw(2, true, 13, 12));
	EXPECT_EQ(kDrawCanvas, ComputeMaterialDraw(0, true, 13, 12));
	EXPECT_EQ(kDrawUserShader, ComputeMaterialDraw(0, false, 12, 12));
	EXPECT_EQ(kDrawPlain, ComputeMaterialDraw(0, false, 3, 12));
}

TEST(MaterialDraw, OnlyAPlainMaterialWithAMatchingBrightmapTakesTheFrame)
{
	EXPECT_TRUE(ComputeMaterialExpands(kDrawPlain, false));
	EXPECT_FALSE(ComputeMaterialExpands(kDrawPlain, true));
	EXPECT_FALSE(ComputeMaterialExpands(kDrawWarp, false));
	EXPECT_FALSE(ComputeMaterialExpands(kDrawUserShader, false));
}
