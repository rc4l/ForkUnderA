// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/app-icon/computation/appicon_compute.h"

#include <gtest/gtest.h>

#include <vector>

using namespace zx::appicon;

// [rc4l] A 4 by 4 of four 2 by 2 blocks becomes a 2 by 2 of their averages.
TEST(AppIcon, EachBlockAveragesToOneTexel)
{
	std::vector<unsigned char> src(4 * 4 * 4, 255);
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
		{
			unsigned char *t = &src[(y * 4 + x) * 4];
			t[0] = (unsigned char)(x < 2 ? 100 : 200);
			t[1] = (unsigned char)(y < 2 ? 10 : 20);
			t[2] = (unsigned char)((x + y) % 2 ? 0 : 80);
		}
	unsigned char dst[2 * 2 * 4];
	ASSERT_TRUE(ComputeIconDownscale(src.data(), 4, 4, 2, dst));
	EXPECT_EQ(100, dst[0]); EXPECT_EQ(10, dst[1]); EXPECT_EQ(40, dst[2]); EXPECT_EQ(255, dst[3]);
	EXPECT_EQ(200, dst[12]); EXPECT_EQ(20, dst[13]); EXPECT_EQ(255, dst[15]);
}

// [rc4l] A clear texel beside an opaque one adds no black to the colour, only to the coverage.
TEST(AppIcon, ClearTexelsAddNoFringe)
{
	const unsigned char src[2 * 1 * 4] = { 240, 0, 0, 255,   0, 0, 0, 0 };
	unsigned char dst[4];
	ASSERT_TRUE(ComputeIconDownscale(src, 2, 1, 1, dst));
	EXPECT_EQ(240, dst[0]);
	EXPECT_EQ(127, dst[3]);
	const unsigned char clear[2 * 2 * 4] = { 0 };
	ASSERT_TRUE(ComputeIconDownscale(clear, 2, 2, 1, dst));
	EXPECT_EQ(0, dst[0]); EXPECT_EQ(0, dst[3]);
}

TEST(AppIcon, APictureSmallerThanTheSizeIsRefused)
{
	const unsigned char src[4] = { 1, 2, 3, 4 };
	unsigned char dst[16];
	EXPECT_FALSE(ComputeIconDownscale(src, 1, 1, 2, dst));
	EXPECT_FALSE(ComputeIconDownscale(src, 1, 1, 0, dst));
}
