// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/materials/computation/texcoord_compute.h"

#include <gtest/gtest.h>

namespace {

// [rc4l] 48.16 fixed point from raw units, 65536 to one.
fixed_t Fx(long long raw) { return fixed_t::FromRaw(raw); }

const fixed_t kOne = Fx(65536);
const fixed_t kHalf = Fx(32768);
const fixed_t kTwo = Fx(131072);
const fixed_t kOffset = Fx(16 * 65536);

} // namespace

TEST(TexCoord, AnUnscaledOffsetIsTheOffset)
{
	EXPECT_EQ(kOffset, ComputeTexCoordOffset(kOffset, kOne, kOne, false));
	// [rc4l] World panning keeps a scaled texture's offset in map units while the sidedef does not scale it.
	EXPECT_EQ(kOffset, ComputeTexCoordOffset(kOffset, kHalf, kOne, true));
}

TEST(TexCoord, TheTexturesScaleDividesTheOffset)
{
	// [rc4l] A texture drawn at half size moves twice as many texels for an offset.
	EXPECT_EQ(Fx(32 * 65536), ComputeTexCoordOffset(kOffset, kHalf, kOne, false));
	EXPECT_EQ(Fx(32 * 65536), ComputeTexCoordOffset(kOffset, kHalf, kTwo, false));
	// [rc4l] With world panning the sidedef's scale divides it instead.
	EXPECT_EQ(Fx(8 * 65536), ComputeTexCoordOffset(kOffset, kHalf, kTwo, true));
}

TEST(TexCoord, TheWidthOffsetsWrapAt)
{
	EXPECT_EQ(Fx(64), ComputeTexAdjustWidth(128, 64, kTwo, false));
	EXPECT_EQ(Fx(128), ComputeTexAdjustWidth(128, 64, kOne, true));
	EXPECT_EQ(Fx(32), ComputeTexAdjustWidth(128, 64, kTwo, true));
}
