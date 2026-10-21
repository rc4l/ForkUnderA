// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/levelmesh/computation/packedcolor_compute.h"

using namespace zx::levelmesh;

TEST(PackedColor, ChannelsComeOutOfTheirOwnBytes)
{
	float r, g, b;
	ComputeUnpackRGB(0x00ff8000u, r, g, b);
	EXPECT_FLOAT_EQ(r, 1.f);
	EXPECT_FLOAT_EQ(g, 128.f / 255.f);
	EXPECT_FLOAT_EQ(b, 0.f);
}

TEST(PackedColor, TheAlphaByteIsIgnored)
{
	// [rc4l] PalEntry keeps alpha in the top byte, and a fog colour carries garbage there.
	float r, g, b;
	ComputeUnpackRGB(0xab102030u, r, g, b);
	EXPECT_FLOAT_EQ(r, 16.f / 255.f);
	EXPECT_FLOAT_EQ(g, 32.f / 255.f);
	EXPECT_FLOAT_EQ(b, 48.f / 255.f);
}

TEST(PackedColor, BlackAndWhiteAreTheEnds)
{
	float r, g, b;
	ComputeUnpackRGB(0u, r, g, b);
	EXPECT_FLOAT_EQ(r + g + b, 0.f);
	ComputeUnpackRGB(0xffffffu, r, g, b);
	EXPECT_FLOAT_EQ(r, 1.f);
	EXPECT_FLOAT_EQ(g, 1.f);
	EXPECT_FLOAT_EQ(b, 1.f);
}
