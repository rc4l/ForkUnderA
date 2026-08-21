// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/surfaces/computation/ffblocks_compute.h"

using namespace zx::surfaces;

namespace {

FFRover Slab(float bottom, float top, bool sides = true, bool inverted = false)
{
	FFRover r;
	r.top[0] = r.top[1] = top;
	r.bottom[0] = r.bottom[1] = bottom;
	r.renderSides = sides;
	r.invertSides = inverted;
	return r;
}

} // namespace

TEST(FFBlocks, OneSlabCutsOneBlock)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[1] = { Slab(64.f, 96.f) };
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFBlocks(top, bottom, slabs, 1, out, 8));
	EXPECT_FLOAT_EQ(96.f, out[0].top[0]);
	EXPECT_FLOAT_EQ(64.f, out[0].bottom[0]);
	EXPECT_EQ(0, out[0].rover);
}

TEST(FFBlocks, SlabsAreWalkedTopDown)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[2] = { Slab(160.f, 192.f), Slab(64.f, 96.f) };
	FFBlock out[8];
	ASSERT_EQ(2, ComputeFFBlocks(top, bottom, slabs, 2, out, 8));
	EXPECT_FLOAT_EQ(192.f, out[0].top[0]);
	EXPECT_FLOAT_EQ(96.f, out[1].top[0]);
}

// [rc4l] A slab reaching above what the previous one left is clipped to it, not drawn over it.
TEST(FFBlocks, AnOverlappingSlabIsClippedToTheRunningTop)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[2] = { Slab(128.f, 192.f), Slab(64.f, 160.f) };
	FFBlock out[8];
	ASSERT_EQ(2, ComputeFFBlocks(top, bottom, slabs, 2, out, 8));
	EXPECT_FLOAT_EQ(128.f, out[1].top[0]);   // clipped down from 160 to where the first slab ended
	EXPECT_FLOAT_EQ(64.f, out[1].bottom[0]);
}

// One entirely above the wall contributes nothing -- while nothing has been drawn yet, which is the
// condition GL writes and the reason the renderedSomething flag exists at all.
TEST(FFBlocks, ASlabAboveTheWallIsSkipped)
{
	const float top[2] = { 128.f, 128.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[2] = { Slab(300.f, 400.f), Slab(32.f, 64.f) };
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFBlocks(top, bottom, slabs, 2, out, 8));
	EXPECT_FLOAT_EQ(64.f, out[0].top[0]);
	EXPECT_EQ(1, out[0].rover);
}

TEST(FFBlocks, SlabsThatDrawNoSidesAreSkipped)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[2] = { Slab(160.f, 192.f, false), Slab(64.f, 96.f) };
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFBlocks(top, bottom, slabs, 2, out, 8));
	EXPECT_EQ(1, out[0].rover);
}

// An inverted slab is the FRONT sector's business -- InverseFloors draws it, from the other side.
TEST(FFBlocks, InvertedSlabsBelongToTheOtherPass)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	const FFRover slabs[1] = { Slab(64.f, 96.f, true, true) };
	FFBlock out[8];
	EXPECT_EQ(0, ComputeFFBlocks(top, bottom, slabs, 1, out, 8));
}

// The walk stops once the running top has reached the wall's bottom: everything below is the wall's
// own lower part, already drawn.
TEST(FFBlocks, TheWalkStopsAtTheWallsBottom)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 128.f, 128.f };
	const FFRover slabs[3] = { Slab(160.f, 192.f), Slab(64.f, 128.f), Slab(0.f, 32.f) };
	FFBlock out[8];
	const int n = ComputeFFBlocks(top, bottom, slabs, 3, out, 8);
	EXPECT_LE(n, 2);
	for (int i = 0; i < n; i++) EXPECT_LT(out[i].rover, 2);
}

// A sloped slab keeps each end at its own height, which is why the pair is passed rather than one
// number -- the same reason the light bands take a pair.
TEST(FFBlocks, ASlopedSlabKeepsBothEnds)
{
	const float top[2] = { 256.f, 256.f }, bottom[2] = { 0.f, 0.f };
	FFRover s = Slab(64.f, 96.f);
	s.top[1] = 160.f; s.bottom[1] = 120.f;
	FFBlock out[8];
	ASSERT_EQ(1, ComputeFFBlocks(top, bottom, &s, 1, out, 8));
	EXPECT_FLOAT_EQ(96.f, out[0].top[0]);
	EXPECT_FLOAT_EQ(160.f, out[0].top[1]);
	EXPECT_FLOAT_EQ(120.f, out[0].bottom[1]);
}

TEST(FFBlocks, TheOutputIsBounded)
{
	const float top[2] = { 1024.f, 1024.f }, bottom[2] = { 0.f, 0.f };
	FFRover slabs[10];
	for (int i = 0; i < 10; i++) slabs[i] = Slab(900.f - i * 100.f, 950.f - i * 100.f);
	FFBlock out[3];
	EXPECT_LE(ComputeFFBlocks(top, bottom, slabs, 10, out, 3), 3);
}
