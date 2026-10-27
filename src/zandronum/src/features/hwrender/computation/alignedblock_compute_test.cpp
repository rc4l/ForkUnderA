// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/hwrender/computation/alignedblock_compute.h"

using namespace zx::hwrender;

// [Doruk-Aksoy] The block starts past its header, on the alignment asked for, and always fits in the bytes taken.
TEST(AlignedBlock, StartsPastTheHeaderOnTheAlignmentAndFits)
{
	for (uintptr_t base = 0x1000; base < 0x1040; base += 8)
		for (size_t alignment : { (size_t)1, (size_t)8, (size_t)16, (size_t)64, (size_t)256 })
		{
			const uintptr_t start = ComputeAlignedBlockStart(base, alignment, 16);
			EXPECT_GE(start, base + 16);
			EXPECT_EQ(0u, start % alignment);
			EXPECT_LE(start + 100, base + ComputeAlignedBlockBytes(100, alignment, 16));
		}
}

// [Doruk-Aksoy] No alignment asked for (0 or 1) puts the block right after the header and takes nothing extra.
TEST(AlignedBlock, NoAlignmentTakesNothingExtra)
{
	EXPECT_EQ(0x1010u, ComputeAlignedBlockStart(0x1000, 0, 16));
	EXPECT_EQ(0x1010u, ComputeAlignedBlockStart(0x1000, 1, 16));
	EXPECT_EQ(116u, ComputeAlignedBlockBytes(100, 1, 16));
	EXPECT_EQ(116u, ComputeAlignedBlockBytes(100, 0, 16));
}
