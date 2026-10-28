// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/readback_compute.h"

#include <gtest/gtest.h>
#include <cmath>
#include <vector>

namespace {

// [rc4l] Two rows of two texels with a padded stride, as a GPU aligns a staging texture's rows.
const std::vector<unsigned char> kRows = {
	1, 2, 3, 255,   4, 5, 6, 255,   99, 99, 99, 99,
	7, 8, 9, 255,   10, 11, 12, 255,  99, 99, 99, 99,
};

TEST(Readback, RgbaTexelsKeepTheirOrderAndSkipThePadding)
{
	std::vector<unsigned char> rgb(2 * 2 * 3, 0);
	zx::ComputeRowsToRGB(kRows.data(), 12, 2, 2, false, rgb.data());
	EXPECT_EQ(rgb, (std::vector<unsigned char>{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 }));
}

TEST(Readback, BgraTexelsSwapRedAndBlue)
{
	std::vector<unsigned char> rgb(2 * 2 * 3, 0);
	zx::ComputeRowsToRGB(kRows.data(), 12, 2, 2, true, rgb.data());
	EXPECT_EQ(rgb, (std::vector<unsigned char>{ 3, 2, 1, 6, 5, 4, 9, 8, 7, 12, 11, 10 }));
}

// [Doruk-Aksoy] Ones, halves, zero and the largest finite half, subnormals, both infinities and a NaN, each as IEEE 754 reads it.
TEST(Readback, HalfFloatsReadAsTheirValues)
{
	EXPECT_EQ(1.0f, zx::ComputeHalfToFloat(0x3C00));
	EXPECT_EQ(0.5f, zx::ComputeHalfToFloat(0x3800));
	EXPECT_EQ(-2.0f, zx::ComputeHalfToFloat(0xC000));
	EXPECT_EQ(0.0f, zx::ComputeHalfToFloat(0x0000));
	EXPECT_EQ(65504.0f, zx::ComputeHalfToFloat(0x7BFF));
	EXPECT_EQ(std::ldexp(1.0f, -24), zx::ComputeHalfToFloat(0x0001));
	EXPECT_TRUE(std::isinf(zx::ComputeHalfToFloat(0x7C00)));
	EXPECT_LT(zx::ComputeHalfToFloat(0xFC00), 0.0f);
	EXPECT_TRUE(std::isnan(zx::ComputeHalfToFloat(0x7E00)));
}

// [Doruk-Aksoy] A 16-bit scene's texels as 8-bit RGB: in-range values rounded, light past 1 and below 0 held to the ends, NaN black, alpha and padding skipped.
TEST(Readback, HalfTexelsBecomeEightBitRGB)
{
	const unsigned short texels[] = {
		0x3C00, 0x3800, 0x0000, 0x3C00,   0x4000, 0xBC00, 0x7E00, 0x3C00,   0x1234, 0x1234, 0x1234, 0x1234,
	};
	std::vector<unsigned char> bytes(sizeof(texels));
	for (size_t i = 0; i < sizeof(texels) / 2; i++) { bytes[i * 2] = (unsigned char)(texels[i] & 255); bytes[i * 2 + 1] = (unsigned char)(texels[i] >> 8); }
	std::vector<unsigned char> rgb(2 * 3, 7);
	zx::ComputeHalfRowsToRGB(bytes.data(), 24, 2, 1, rgb.data());
	EXPECT_EQ(rgb, (std::vector<unsigned char>{ 255, 128, 0, 255, 0, 0 }));
}

} // namespace
