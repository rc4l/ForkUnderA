// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/walldecal_compute.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace zx::levelmesh;

namespace {

// [rc4l] A 128-unit wall along x from 0 to 128, 0 to 128 high, the whole line.
DecalWall Wall()
{
	const DecalWall w = { 0, 0, 128, 0, 0.f, 1.f, 128.f, { 128, 128 }, { 0, 0 } };
	return w;
}

// [rc4l] A 32 by 16 decal with its offsets at its middle, at the line's middle, its reference line at 64.
DecalPicture Picture()
{
	const DecalPicture d = { 32, 16, 16, 8, 1.f, 32.f, (float)(1 << 29), 64.0, false, false };
	return d;
}

} // namespace

TEST(WallDecal, HeightFollowsThePartAndThePegging)
{
	DecalHeightIn in = { kDecalRelUpper, kDecalPartUpper, false, false, 4.0, 128.0, 0.0, 96.0, 16.0 };
	double z = 0;
	EXPECT_TRUE(ComputeDecalHeight(in, z));
	EXPECT_EQ(100.0, z);
	in.unpegTop = true;
	EXPECT_TRUE(ComputeDecalHeight(in, z));
	EXPECT_EQ(132.0, z);
	in.part = kDecalPartMiddle;
	EXPECT_FALSE(ComputeDecalHeight(in, z));

	in.rel = kDecalRelLower;
	in.part = kDecalPartLower;
	EXPECT_TRUE(ComputeDecalHeight(in, z));
	EXPECT_EQ(20.0, z);
	in.unpegBottom = true;
	EXPECT_TRUE(ComputeDecalHeight(in, z));
	EXPECT_EQ(132.0, z);
	in.part = kDecalPartUpper;
	EXPECT_FALSE(ComputeDecalHeight(in, z));

	in.rel = kDecalRelMid;
	in.part = kDecalPartMiddle;
	EXPECT_TRUE(ComputeDecalHeight(in, z));
	EXPECT_EQ(4.0, z);
	in.unpegBottom = false;
	EXPECT_TRUE(ComputeDecalHeight(in, z));
	EXPECT_EQ(132.0, z);
	in.part = kDecalPartLower;
	EXPECT_FALSE(ComputeDecalHeight(in, z));

	in.rel = kDecalRelNone;
	EXPECT_FALSE(ComputeDecalHeight(in, z));
}

TEST(WallDecal, OnlyPlainWallsTakeDecals)
{
	EXPECT_TRUE(ComputeDecalsOnPart(false, true, true));
	EXPECT_TRUE(ComputeDecalsOnPart(true, false, false));
	EXPECT_FALSE(ComputeDecalsOnPart(true, true, false));
	EXPECT_FALSE(ComputeDecalsOnPart(true, false, true));
}

TEST(WallDecal, AQuadSitsWhereItsOffsetsPutIt)
{
	DecalCorner dv[4];
	double top = 0;
	ASSERT_TRUE(ComputeWallDecalQuad(Wall(), Picture(), dv, top));
	EXPECT_EQ(72.0, top);
	EXPECT_FLOAT_EQ(48.f, dv[0].x);
	EXPECT_FLOAT_EQ(48.f, dv[1].x);
	EXPECT_FLOAT_EQ(80.f, dv[2].x);
	EXPECT_FLOAT_EQ(80.f, dv[3].x);
	EXPECT_FLOAT_EQ(0.f, dv[0].y);
	EXPECT_FLOAT_EQ(56.f, dv[0].z);
	EXPECT_FLOAT_EQ(72.f, dv[1].z);
	EXPECT_FLOAT_EQ(72.f, dv[2].z);
	EXPECT_FLOAT_EQ(56.f, dv[3].z);
	EXPECT_FLOAT_EQ(0.f, dv[0].u);
	EXPECT_FLOAT_EQ(1.f, dv[2].u);
	EXPECT_FLOAT_EQ(1.f, dv[0].v);
	EXPECT_FLOAT_EQ(0.f, dv[1].v);
}

TEST(WallDecal, TheEndsOfTheFragmentCutTheQuad)
{
	DecalCorner dv[4];
	double top = 0;
	// [rc4l] Off the left end: its left eight texels are cut and its u starts a quarter in.
	DecalPicture d = Picture();
	d.leftDistance = (float)(1 << 30) * (8.f / 128.f);
	ASSERT_TRUE(ComputeWallDecalQuad(Wall(), d, dv, top));
	EXPECT_FLOAT_EQ(0.f, dv[0].x);
	EXPECT_FLOAT_EQ(0.25f, dv[0].u);
	EXPECT_FLOAT_EQ(24.f, dv[2].x);
	// [rc4l] Off the right end.
	d.leftDistance = (float)(1 << 30) * (124.f / 128.f);
	ASSERT_TRUE(ComputeWallDecalQuad(Wall(), d, dv, top));
	EXPECT_FLOAT_EQ(108.f, dv[0].x);
	EXPECT_FLOAT_EQ(128.f, dv[2].x);
	EXPECT_FLOAT_EQ(20.f / 32.f, dv[2].u);
	// [rc4l] Wholly past it, or on a fragment that starts further along.
	d.leftDistance = (float)(1 << 30) * 1.5f;
	EXPECT_FALSE(ComputeWallDecalQuad(Wall(), d, dv, top));
	DecalWall half = Wall();
	half.x2 = 64;
	half.fracRight = 0.5f;
	d.leftDistance = (float)(1 << 30) * (100.f / 128.f);
	EXPECT_FALSE(ComputeWallDecalQuad(half, d, dv, top));
}

TEST(WallDecal, TheFragmentsTopAndBottomCutTheQuad)
{
	DecalCorner dv[4];
	double top = 0;
	// [rc4l] A wall topping out at 68 takes the decal's top quarter off, and its v there with it.
	DecalWall w = Wall();
	w.ztop[0] = w.ztop[1] = 68;
	ASSERT_TRUE(ComputeWallDecalQuad(w, Picture(), dv, top));
	EXPECT_FLOAT_EQ(68.f, dv[1].z);
	EXPECT_FLOAT_EQ(68.f, dv[2].z);
	EXPECT_FLOAT_EQ(0.25f, dv[1].v);
	EXPECT_FLOAT_EQ(0.25f, dv[2].v);
	// [rc4l] And one bottoming out at 60 its bottom quarter.
	w = Wall();
	w.zbottom[0] = w.zbottom[1] = 60;
	ASSERT_TRUE(ComputeWallDecalQuad(w, Picture(), dv, top));
	EXPECT_FLOAT_EQ(60.f, dv[0].z);
	EXPECT_FLOAT_EQ(60.f, dv[3].z);
	EXPECT_FLOAT_EQ(0.75f, dv[0].v);
	EXPECT_FLOAT_EQ(0.75f, dv[3].v);
	// [rc4l] Wholly above or below the fragment.
	w = Wall();
	w.ztop[0] = w.ztop[1] = 40;
	EXPECT_FALSE(ComputeWallDecalQuad(w, Picture(), dv, top));
	w = Wall();
	w.zbottom[0] = w.zbottom[1] = 90;
	EXPECT_FALSE(ComputeWallDecalQuad(w, Picture(), dv, top));
}

TEST(WallDecal, FlipsMirrorTheTextureAndTheOffsets)
{
	DecalCorner dv[4];
	double top = 0;
	DecalPicture d = Picture();
	d.leftOffset = 4;
	d.topOffset = 2;
	d.flipx = d.flipy = true;
	ASSERT_TRUE(ComputeWallDecalQuad(Wall(), d, dv, top));
	// [rc4l] Flipped, the offsets count from the other edges.
	EXPECT_FLOAT_EQ(36.f, dv[0].x);
	EXPECT_EQ(78.0, top);
	EXPECT_FLOAT_EQ(1.f, dv[0].u);
	EXPECT_FLOAT_EQ(0.f, dv[2].u);
	EXPECT_FLOAT_EQ(0.f, dv[0].v);
	EXPECT_FLOAT_EQ(1.f, dv[1].v);
}

TEST(WallDecal, TheTopOffsetIsAddedInFixedPoint)
{
	DecalCorner dv[4];
	double top = 0;
	DecalPicture d = Picture();
	d.topOffset = 8.3f;
	ASSERT_TRUE(ComputeWallDecalQuad(Wall(), d, dv, top));
	EXPECT_EQ(64.0 + std::trunc(65536.0 * 8.3f) / 65536.0, top);
}
