// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/readback_compute.h"

#include <gtest/gtest.h>
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

} // namespace
