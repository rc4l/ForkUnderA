// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/materials/computation/texturepixels_compute.h"

#include <gtest/gtest.h>

#include <vector>

using namespace zx::materials;

namespace {

// [rc4l] An RGBA picture `w` by `h`, all texels clear black.
std::vector<unsigned char> Clear(int w, int h) { return std::vector<unsigned char>(w * h * 4, 0); }

void Set(std::vector<unsigned char> &p, int w, int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	unsigned char *t = &p[(y * w + x) * 4];
	t[0] = r; t[1] = g; t[2] = b; t[3] = a;
}

} // namespace

TEST(TexturePixels, NoTexelsAverageWhite)
{
	EXPECT_EQ(0xFFFFFFFFu, ComputeAverageColor(NULL, 0, false, 0));
}

TEST(TexturePixels, TheAverageIsEachChannelsMean)
{
	auto p = Clear(2, 1);
	Set(p, 2, 0, 0, 100, 20, 0, 255);
	Set(p, 2, 1, 0, 50, 40, 10, 255);
	EXPECT_EQ(0xFF4B1E05u, ComputeAverageColor(p.data(), 2, false, 0));
	// [rc4l] Scaling only applies when asked.
	EXPECT_EQ(0xFF4B1E05u, ComputeAverageColor(p.data(), 2, false, 255));
}

// [rc4l] A glow's colour is stretched so its brightest channel reaches the limit, keeping the others in proportion.
TEST(TexturePixels, ScalingTakesTheBrightestChannelToTheLimit)
{
	auto p = Clear(1, 1);
	Set(p, 1, 0, 0, 100, 50, 25, 255);
	EXPECT_EQ(0xFF994C26u, ComputeAverageColor(p.data(), 1, true, 153));
	// [rc4l] A black picture has no brightest channel to scale.
	auto black = Clear(1, 1);
	EXPECT_EQ(0xFF000000u, ComputeAverageColor(black.data(), 1, true, 153));
}

TEST(TexturePixels, KnownTransparencyIsKept)
{
	auto p = Clear(1, 1);
	EXPECT_EQ(1, ComputeTransparency(p.data(), 1, 1));
	EXPECT_EQ(0, ComputeTransparency(p.data(), 1, 0));
}

TEST(TexturePixels, OnlyPartialAlphaIsTransparent)
{
	auto p = Clear(3, 1);
	Set(p, 3, 1, 0, 0, 0, 0, 255);
	EXPECT_EQ(0, ComputeTransparency(p.data(), 3, -1));
	Set(p, 3, 2, 0, 0, 0, 0, 128);
	EXPECT_EQ(1, ComputeTransparency(p.data(), 3, -1));
}

TEST(TexturePixels, ALineOfTexelsIsNotSmoothed)
{
	auto p = Clear(4, 1);
	EXPECT_FALSE(ComputeSmoothEdges(p.data(), 4, 1));
	EXPECT_FALSE(ComputeSmoothEdges(p.data(), 1, 4));
}

// [rc4l] Round one opaque texel of a 4 by 4, the clear texels beside it take its colour and stay clear, and of the diagonal ones only the inner one does.
TEST(TexturePixels, ClearTexelsTakeANeighboursColour)
{
	auto p = Clear(4, 4);
	Set(p, 4, 1, 1, 200, 0, 0, 255);
	EXPECT_TRUE(ComputeSmoothEdges(p.data(), 4, 4));
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
		{
			const unsigned char *t = &p[(y * 4 + x) * 4];
			const bool touches = (x == 1 && y == 0) || (x == 0 && y == 1) || (x == 1 && y == 1) || (x == 2 && y == 1) || (x == 1 && y == 2) || (x == 2 && y == 2);
			EXPECT_EQ(touches ? 200 : 0, t[0]) << x << "," << y;
			EXPECT_EQ((x == 1 && y == 1) ? 255 : 0, t[3]) << x << "," << y;
		}
}

// [rc4l] Opaque texels along the edges reach the clear corners and edges of a 3 by 3 from each side.
TEST(TexturePixels, EdgeTexelsSmoothFromEverySide)
{
	auto p = Clear(3, 3);
	Set(p, 3, 1, 0, 10, 0, 0, 255);
	Set(p, 3, 0, 1, 20, 0, 0, 255);
	Set(p, 3, 2, 1, 30, 0, 0, 255);
	Set(p, 3, 1, 2, 40, 0, 0, 255);
	EXPECT_TRUE(ComputeSmoothEdges(p.data(), 3, 3));
	EXPECT_EQ(10, p[0]);                    // [rc4l] the top-left corner from its right
	EXPECT_EQ(10, p[(0 * 3 + 2) * 4]);      // [rc4l] the top-right corner from its left
	EXPECT_EQ(20, p[(2 * 3 + 0) * 4]);      // [rc4l] the bottom-left corner from above
	EXPECT_EQ(30, p[(2 * 3 + 2) * 4]);      // [rc4l] the bottom-right corner from above
	EXPECT_EQ(10, p[(1 * 3 + 1) * 4]);      // [rc4l] the middle from above
}

// [rc4l] A clear texel takes the first opaque neighbour it looks at: above, then left, then right, then below.
TEST(TexturePixels, ClearTexelsLookBelowLast)
{
	auto p = Clear(3, 3);
	Set(p, 3, 0, 1, 50, 0, 0, 255);
	Set(p, 3, 1, 2, 60, 0, 0, 255);
	EXPECT_TRUE(ComputeSmoothEdges(p.data(), 3, 3));
	EXPECT_EQ(50, p[0]);                    // [rc4l] the top-left corner from below
	EXPECT_EQ(50, p[(1 * 3 + 1) * 4]);      // [rc4l] the middle from its left, before below
	EXPECT_EQ(50, p[(2 * 3 + 0) * 4]);      // [rc4l] the bottom-left corner from above, before its right
	EXPECT_EQ(60, p[(2 * 3 + 2) * 4]);      // [rc4l] the bottom-right corner from its left
	auto q = Clear(3, 3);
	Set(q, 3, 1, 1, 70, 0, 0, 255);
	Set(q, 3, 2, 1, 80, 0, 0, 255);
	EXPECT_TRUE(ComputeSmoothEdges(q.data(), 3, 3));
	EXPECT_EQ(70, q[(0 * 3 + 1) * 4]);      // [rc4l] the top edge from below
	EXPECT_EQ(80, q[(0 * 3 + 2) * 4]);      // [rc4l] the top-right corner from below
	EXPECT_EQ(70, q[(2 * 3 + 1) * 4]);      // [rc4l] the bottom edge from above
}

TEST(TexturePixels, AnOpaquePictureHasNoClearTexels)
{
	std::vector<unsigned char> p(2 * 2 * 4, 255);
	EXPECT_FALSE(ComputeSmoothEdges(p.data(), 2, 2));
}

// [rc4l] Rows 0-1 and 40-41 opaque in a 64-row picture are two bands, each at least two rows.
TEST(TexturePixels, OpaqueRowsMakeBands)
{
	auto p = Clear(2, 64);
	Set(p, 2, 1, 0, 0, 0, 0, 255);
	Set(p, 2, 0, 40, 0, 0, 0, 255);
	int bands[5][2];
	ASSERT_EQ(2, ComputeTextureBands(p.data(), 2, 64, bands));
	EXPECT_EQ(0, bands[0][0]); EXPECT_EQ(2, bands[0][1]);
	EXPECT_EQ(40, bands[1][0]); EXPECT_EQ(2, bands[1][1]);
}

// [rc4l] A gap of 16 rows or fewer joins its band to the one above, and a band reaching the bottom closes there.
TEST(TexturePixels, ShortGapsMergeBands)
{
	auto p = Clear(1, 40);
	for (int y : { 0, 1, 10, 11 }) Set(p, 1, 0, y, 0, 0, 0, 255);
	for (int y = 30; y < 40; y++) Set(p, 1, 0, y, 0, 0, 0, 255);
	int bands[5][2];
	ASSERT_EQ(2, ComputeTextureBands(p.data(), 1, 40, bands));
	EXPECT_EQ(0, bands[0][0]); EXPECT_EQ(12, bands[0][1]);
	EXPECT_EQ(30, bands[1][0]); EXPECT_EQ(10, bands[1][1]);
}

TEST(TexturePixels, OneWholeBandOrTooManyAreNotSplit)
{
	std::vector<unsigned char> full(1 * 8 * 4, 255);
	int bands[5][2];
	EXPECT_EQ(0, ComputeTextureBands(full.data(), 1, 8, bands));
	// [rc4l] Five bands 20 rows apart are more than splitting is worth.
	auto many = Clear(1, 100);
	for (int y = 0; y < 100; y += 20) Set(many, 1, 0, y, 0, 0, 0, 255);
	EXPECT_EQ(0, ComputeTextureBands(many.data(), 1, 100, bands));
}

// [rc4l] Entry 5 maps to itself at every light level and is bright, so it is lit; 6 darkens, and 0 is near black.
TEST(TexturePixels, FullbrightEntriesKeepTheirIndexAtEveryLevel)
{
	std::vector<unsigned char> colormap(256 * 32);
	for (int j = 0; j < 32; j++)
		for (int i = 0; i < 256; i++) colormap[i + j * 256] = (unsigned char)((i == 5 || i == 0 || j == 0) ? i : i + 1);
	std::vector<unsigned char> playpal(256 * 3, 200);
	playpal[0] = playpal[1] = playpal[2] = 0;
	bool lit[256];
	EXPECT_EQ(1, ComputeFullbrightEntries(colormap.data(), playpal.data(), lit));
	EXPECT_TRUE(lit[5]);
	EXPECT_FALSE(lit[6]);
	EXPECT_FALSE(lit[0]);
}
