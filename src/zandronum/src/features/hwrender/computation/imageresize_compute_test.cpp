// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/hwrender/computation/imageresize_compute.h"

#include <vector>
#include <gtest/gtest.h>

namespace
{

// Build a solid RGBA image.
std::vector<unsigned char> Solid(int w, int h, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	std::vector<unsigned char> img((size_t)w * h * 4);
	for (size_t i = 0; i < img.size(); i += 4) { img[i] = r; img[i+1] = g; img[i+2] = b; img[i+3] = a; }
	return img;
}

TEST(ImageResize, SolidColorSurvivesDownscale)
{
	auto src = Solid(8, 8, 200, 100, 50, 255);
	std::vector<unsigned char> dst(4 * 4 * 4, 1);
	zx::ResizeImageBoxAverage(8, 8, src.data(), 4, 4, dst.data());
	for (size_t i = 0; i < dst.size(); i += 4)
	{
		EXPECT_EQ(dst[i], 200); EXPECT_EQ(dst[i+1], 100);
		EXPECT_EQ(dst[i+2], 50); EXPECT_EQ(dst[i+3], 255);
	}
}

TEST(ImageResize, AveragesTwoHalves)
{
	// Left half black, right half white, both opaque; 2:1 downscale to 2x1: each dest pixel's box
	// stays within its own half, so left is black, right is white.
	std::vector<unsigned char> src = {
		0,0,0,255,  0,0,0,255,  255,255,255,255,  255,255,255,255,
	};
	std::vector<unsigned char> dst(2 * 1 * 4, 7);
	zx::ResizeImageBoxAverage(4, 1, src.data(), 2, 1, dst.data());
	EXPECT_EQ(dst[0], 0); EXPECT_EQ(dst[3], 255);
	EXPECT_EQ(dst[4], 255); EXPECT_EQ(dst[7], 255);
}

// [rc4l] The upstream bug this port fixes: a box of fully transparent pixels divided by zero.
// Here it must produce transparent black, deterministically.
TEST(ImageResize, FullyTransparentBoxIsTransparentBlack)
{
	auto src = Solid(4, 4, 90, 90, 90, 0);
	std::vector<unsigned char> dst(2 * 2 * 4, 9);
	zx::ResizeImageBoxAverage(4, 4, src.data(), 2, 2, dst.data());
	for (size_t i = 0; i < dst.size(); i++) EXPECT_EQ(dst[i], 0);
}

TEST(ImageResize, TransparentPixelsDoNotDarkenColor)
{
	// One opaque red pixel among transparent ones: colour must stay pure red (no black bleed),
	// alpha averaged over the whole box.
	std::vector<unsigned char> src = {
		255,0,0,255,  0,0,0,0,
		0,0,0,0,      0,0,0,0,
	};
	std::vector<unsigned char> dst(1 * 1 * 4, 3);
	zx::ResizeImageBoxAverage(2, 2, src.data(), 1, 1, dst.data());
	EXPECT_EQ(dst[0], 255);
	EXPECT_EQ(dst[1], 0);
	EXPECT_EQ(dst[2], 0);
	EXPECT_EQ(dst[3], 64); // 255 over a 4-pixel box, rounded
}

TEST(ImageResize, RejectsDegenerateDimensions)
{
	std::vector<unsigned char> dst(4, 42);
	zx::ResizeImageBoxAverage(0, 4, nullptr, 1, 1, dst.data());
	zx::ResizeImageBoxAverage(4, 0, nullptr, 1, 1, dst.data());
	zx::ResizeImageBoxAverage(4, 4, nullptr, 0, 1, dst.data());
	zx::ResizeImageBoxAverage(4, 4, nullptr, 1, 0, dst.data());
	// untouched
	for (size_t i = 0; i < dst.size(); i++) EXPECT_EQ(dst[i], 42);
}

} // namespace
