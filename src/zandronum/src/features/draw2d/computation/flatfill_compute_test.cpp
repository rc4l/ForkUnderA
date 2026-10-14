// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/computation/flatfill_compute.h"

#include <gtest/gtest.h>

namespace {

using zx::draw2d::ComputeFlatTiles;
using zx::draw2d::FlatTile;

void ExpectTile(const FlatTile &t, float x, float y, float w, float h, float u1, float v1, float u2, float v2)
{
	EXPECT_FLOAT_EQ(t.x, x); EXPECT_FLOAT_EQ(t.y, y); EXPECT_FLOAT_EQ(t.w, w); EXPECT_FLOAT_EQ(t.h, h);
	EXPECT_FLOAT_EQ(t.u1, u1); EXPECT_FLOAT_EQ(t.v1, v1); EXPECT_FLOAT_EQ(t.u2, u2); EXPECT_FLOAT_EQ(t.v2, v2);
}

TEST(ComputeFlatTiles, NothingForAnEmptyRectangleOrTexture)
{
	std::vector<FlatTile> out(3);
	ComputeFlatTiles(10, 10, 10, 50, 64, 64, false, out);
	EXPECT_TRUE(out.empty());
	ComputeFlatTiles(10, 10, 50, 5, 64, 64, false, out);
	EXPECT_TRUE(out.empty());
	ComputeFlatTiles(0, 0, 50, 50, 0, 64, false, out);
	EXPECT_TRUE(out.empty());
	ComputeFlatTiles(0, 0, 50, 50, 64, 0, false, out);
	EXPECT_TRUE(out.empty());
}

TEST(ComputeFlatTiles, OneTileInsideATextureShowsItsPart)
{
	std::vector<FlatTile> out;
	ComputeFlatTiles(16, 32, 48, 64, 64, 64, false, out);
	ASSERT_EQ(out.size(), 1u);
	ExpectTile(out[0], 16, 32, 32, 32, 0.25f, 0.5f, 0.75f, 1.f);
}

TEST(ComputeFlatTiles, TheScreenGridCutsAtTheTexturesEdges)
{
	// [rc4l] The status bar's flank: 100 wide from x=0 over a 64-wide flat is one whole copy and part of the next.
	std::vector<FlatTile> out;
	ComputeFlatTiles(0, 0, 100, 64, 64, 64, false, out);
	ASSERT_EQ(out.size(), 2u);
	ExpectTile(out[0], 0, 0, 64, 64, 0.f, 0.f, 1.f, 1.f);
	ExpectTile(out[1], 64, 0, 36, 64, 0.f, 0.f, 36.f / 64.f, 1.f);
}

TEST(ComputeFlatTiles, ALocalOriginStartsTheGridAtTheRectangle)
{
	std::vector<FlatTile> out;
	ComputeFlatTiles(10, 20, 74, 84, 64, 64, true, out);
	ASSERT_EQ(out.size(), 1u);
	ExpectTile(out[0], 10, 20, 64, 64, 0.f, 0.f, 1.f, 1.f);
	ComputeFlatTiles(10, 20, 74, 84, 64, 64, false, out);
	ASSERT_EQ(out.size(), 4u);
	ExpectTile(out[0], 10, 20, 54, 44, 10.f / 64.f, 20.f / 64.f, 1.f, 1.f);
	ExpectTile(out[3], 64, 64, 10, 20, 0.f, 0.f, 10.f / 64.f, 20.f / 64.f);
}

TEST(ComputeFlatTiles, ARectangleLeftOfTheOriginLandsInTheTileBefore)
{
	std::vector<FlatTile> out;
	ComputeFlatTiles(-16, -8, 0, 0, 64, 32, false, out);
	ASSERT_EQ(out.size(), 1u);
	ExpectTile(out[0], -16, -8, 16, 8, 0.75f, 0.75f, 1.f, 1.f);
}

} // namespace
