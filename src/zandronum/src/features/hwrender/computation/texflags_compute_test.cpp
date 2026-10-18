// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/texflags_compute.h"

#include <gtest/gtest.h>

using namespace zx::hwrender;

namespace {

PieceTexInputs None()
{
	PieceTexInputs in = { false, false, false, false, false, false, false, 0, false, 0, false, false };
	return in;
}

} // namespace

// [rc4l] The values the scene shaders test, pinned, since a flag moved on one side alone draws wrong with no error.
TEST(TexFlags, EachFlagIsTheBitTheShadersTest)
{
	EXPECT_EQ(0u, ComputePieceTexFlags(None()));
	PieceTexInputs in = None(); in.stencil = true; EXPECT_EQ(1u, ComputePieceTexFlags(in));
	in = None(); in.alphaTest = true; EXPECT_EQ(2u, ComputePieceTexFlags(in));
	in = None(); in.hud = true; EXPECT_EQ(4u, ComputePieceTexFlags(in));
	in = None(); in.level = true; EXPECT_EQ(8u, ComputePieceTexFlags(in));
	in = None(); in.inverse = true; EXPECT_EQ(16u, ComputePieceTexFlags(in));
	in = None(); in.colorBlend = true; EXPECT_EQ(32u, ComputePieceTexFlags(in));
	in = None(); in.fogLayer = true; EXPECT_EQ(64u, ComputePieceTexFlags(in));
	in = None(); in.fogBoundary = true; EXPECT_EQ(1024u, ComputePieceTexFlags(in));
	in = None(); in.fogPass = true; EXPECT_EQ(8192u, ComputePieceTexFlags(in));
	in = None(); in.noMarks = true; EXPECT_EQ(16384u, ComputePieceTexFlags(in));
}

// [rc4l] The fuzz shader and the culled faces each read back from their own bits, and a value too big for its field cannot spill into the next.
TEST(TexFlags, FuzzAndCullKeepToTheirOwnBits)
{
	PieceTexInputs in = None();
	in.fuzz = 7; in.cullFace = 2; in.level = true;
	const unsigned f = ComputePieceTexFlags(in);
	EXPECT_EQ(7u, (f >> 7) & 7);
	EXPECT_EQ(2u, (f >> 11) & 3);
	EXPECT_EQ(8u, f & 8);
	in = None(); in.fuzz = 9; in.cullFace = 5;
	EXPECT_EQ((1u << 7) | (1u << 11), ComputePieceTexFlags(in));
}
