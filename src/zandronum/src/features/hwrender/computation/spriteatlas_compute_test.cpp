// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/spriteatlas_compute.h"

#include <gtest/gtest.h>

using namespace zx::hwrender;

TEST(SpriteAtlasCompute, RectanglesFillAShelfThenStartTheNext)
{
	std::vector<AtlasPage> pages;
	AtlasPlace a, b, c;
	ASSERT_TRUE(ComputeAtlasPlace(pages, 2, 100, 100, 60, 30, a));
	ASSERT_TRUE(ComputeAtlasPlace(pages, 2, 100, 100, 40, 20, b));
	ASSERT_TRUE(ComputeAtlasPlace(pages, 2, 100, 100, 50, 30, c));
	EXPECT_EQ(0, a.page); EXPECT_EQ(0, a.x); EXPECT_EQ(0, a.y);
	EXPECT_EQ(0, b.page); EXPECT_EQ(60, b.x); EXPECT_EQ(0, b.y) << "beside the first, on a shelf no more than twice its height";
	EXPECT_EQ(0, c.page); EXPECT_EQ(0, c.x); EXPECT_EQ(30, c.y) << "no room left on the first shelf";
	ASSERT_EQ(1u, pages.size());
	EXPECT_EQ(60, pages[0].top);
}

// [rc4l] A tiny sprite on a tall shelf would waste the shelf's height under it.
TEST(SpriteAtlasCompute, AShortRectangleStartsItsOwnShelf)
{
	std::vector<AtlasPage> pages;
	AtlasPlace a, b;
	ASSERT_TRUE(ComputeAtlasPlace(pages, 1, 100, 100, 10, 50, a));
	ASSERT_TRUE(ComputeAtlasPlace(pages, 1, 100, 100, 10, 10, b));
	EXPECT_EQ(50, b.y);
	EXPECT_EQ(0, b.x);
}

TEST(SpriteAtlasCompute, AFullPageSpillsToTheNextAndTheLastRefuses)
{
	std::vector<AtlasPage> pages;
	AtlasPlace p;
	ASSERT_TRUE(ComputeAtlasPlace(pages, 2, 64, 64, 64, 64, p));
	EXPECT_EQ(0, p.page);
	ASSERT_TRUE(ComputeAtlasPlace(pages, 2, 64, 64, 64, 40, p));
	EXPECT_EQ(1, p.page);
	EXPECT_EQ(0, p.y);
	EXPECT_FALSE(ComputeAtlasPlace(pages, 2, 64, 64, 64, 40, p)) << "both pages full";
	EXPECT_FALSE(ComputeAtlasPlace(pages, 2, 64, 64, 65, 1, p)) << "wider than a page";
	EXPECT_FALSE(ComputeAtlasPlace(pages, 2, 64, 64, 1, 65, p)) << "taller than a page";
	EXPECT_FALSE(ComputeAtlasPlace(pages, 2, 64, 64, 0, 4, p));
	EXPECT_FALSE(ComputeAtlasPlace(pages, 2, 64, 64, 4, 0, p));
}

TEST(SpriteAtlasCompute, CoordinatesLandInTheImagesRectangle)
{
	float u = 0.f, v = 1.f;
	ComputeAtlasUV(100, 200, 50, 40, 1000, 2000, u, v);
	EXPECT_FLOAT_EQ(0.1f, u);
	EXPECT_FLOAT_EQ(0.12f, v);
	u = 1.f; v = 0.5f;
	ComputeAtlasUV(100, 200, 50, 40, 1000, 2000, u, v);
	EXPECT_FLOAT_EQ(0.15f, u);
	EXPECT_FLOAT_EQ(0.11f, v);
}

TEST(SpriteAtlasCompute, OnlyPlainPointSampledSpritesOnThePerMaterialPathDrawFromIt)
{
	const AtlasCandidate yes = { true, true, true, true, 64, 128 };
	EXPECT_TRUE(ComputeAtlasEligible(yes, 512));
	AtlasCandidate c = yes; c.perMaterial = false;
	EXPECT_FALSE(ComputeAtlasEligible(c, 512)) << "the material array already carries every texture";
	c = yes; c.pointSampled = false;
	EXPECT_FALSE(ComputeAtlasEligible(c, 512)) << "filtering or mipmaps would sample the neighbours";
	c = yes; c.sprite = false;
	EXPECT_FALSE(ComputeAtlasEligible(c, 512));
	c = yes; c.plainTexture = false;
	EXPECT_FALSE(ComputeAtlasEligible(c, 512));
	c = yes; c.width = 513;
	EXPECT_FALSE(ComputeAtlasEligible(c, 512));
	c = yes; c.height = 513;
	EXPECT_FALSE(ComputeAtlasEligible(c, 512));
	c = yes; c.width = 0;
	EXPECT_FALSE(ComputeAtlasEligible(c, 512));
	c = yes; c.height = 0;
	EXPECT_FALSE(ComputeAtlasEligible(c, 512));
}

TEST(SpriteAtlasCompute, AnImageIsFramedInTransparentTexels)
{
	const unsigned char px[2 * 1 * 4] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	std::vector<unsigned char> out;
	ComputeAtlasFramed(px, 2, 1, out);
	ASSERT_EQ(4u * 3u * 4u, out.size());
	for (int i = 0; i < 16; i++) EXPECT_EQ(0, out[i]) << "the top row is border";
	EXPECT_EQ(0, out[16]) << "the left border of the image's row";
	EXPECT_EQ(1, out[20]);
	EXPECT_EQ(8, out[27]);
	EXPECT_EQ(0, out[28]) << "the right border";
	for (int i = 32; i < 48; i++) EXPECT_EQ(0, out[i]) << "the bottom row is border";
}
