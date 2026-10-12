// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/computation/skywall_compute.h"

#include <gtest/gtest.h>

using namespace zx::surfaces;

namespace {

SkyTopIn SkyCeiling()
{
	SkyTopIn in = {};
	in.frontSky = true;
	in.hasBottom = true;
	in.midExists = true;
	in.zceil[0] = 128.f; in.zceil[1] = 136.f;
	in.backCeiling[0] = 96.f; in.backCeiling[1] = 100.f;
	in.backCeilingPlusMid = 112.f;
	return in;
}

SkyBottomIn SkyFloor()
{
	SkyBottomIn in = {};
	in.frontSky = true;
	in.zfloor[0] = -16.f; in.zfloor[1] = -8.f;
	in.backFloor[0] = 0.f; in.backFloor[1] = 4.f;
	return in;
}

} // namespace

TEST(SkyWall, AOneSidedWallReachesTheLimitAboveAndBelow)
{
	const float floor[2] = { 0.f, 8.f }, ceil[2] = { 128.f, 120.f };
	SkyWallQuad q[2];
	ComputeSkyNormal(floor, ceil, q);
	EXPECT_TRUE(q[0].ceiling);
	EXPECT_EQ(q[0].zbottom[0], 128.f); EXPECT_EQ(q[0].zbottom[1], 120.f);
	EXPECT_EQ(q[0].ztop[0], 32768.f);
	EXPECT_FALSE(q[1].ceiling);
	EXPECT_EQ(q[1].zbottom[1], -32768.f);
	EXPECT_EQ(q[1].ztop[0], 0.f); EXPECT_EQ(q[1].ztop[1], 8.f);
	EXPECT_TRUE(q[0].allowReflect && q[1].allowReflect);
}

TEST(SkyWall, ASkyCeilingRisesFromTheFrontCeiling)
{
	SkyWallQuad q;
	ASSERT_TRUE(ComputeSkyTop(SkyCeiling(), q));
	EXPECT_EQ(q.zbottom[0], 128.f); EXPECT_EQ(q.zbottom[1], 136.f);
	EXPECT_EQ(q.ztop[1], 32768.f);
	EXPECT_FALSE(q.skyhack);
	SkyTopIn in = SkyCeiling();
	in.backNoSkyDraw = true;
	EXPECT_FALSE(ComputeSkyTop(in, q));
}

TEST(SkyWall, SkyOnBothSidesOnlyWallsAShutBackSector)
{
	SkyTopIn in = SkyCeiling();
	in.backSky = true;
	in.backOpen = true;
	SkyWallQuad q;
	EXPECT_FALSE(ComputeSkyTop(in, q));
	in.backOpen = false;
	ASSERT_TRUE(ComputeSkyTop(in, q));
	EXPECT_EQ(q.zbottom[0], 96.f);   // [rc4l] from the back sector's ceiling, with GL's sky hack
	EXPECT_TRUE(q.skyhack);
}

TEST(SkyWall, TheIcarusDoorHack)
{
	SkyTopIn in = SkyCeiling();
	in.backSky = true;
	in.icarus = true;
	in.midOffsetDown = true;
	SkyWallQuad q;
	ASSERT_TRUE(ComputeSkyTop(in, q));
	EXPECT_EQ(q.zbottom[0], 112.f); EXPECT_EQ(q.zbottom[1], 112.f);
	EXPECT_FALSE(q.allowReflect);
	in.dontPegTop = true;   // [rc4l] not hung from the top: the ordinary sky hack
	ASSERT_TRUE(ComputeSkyTop(in, q));
	EXPECT_TRUE(q.skyhack);
	in.hasBottom = false;   // [rc4l] no lower texture: no wall at all
	EXPECT_FALSE(ComputeSkyTop(in, q));
}

TEST(SkyWall, StackedAndReflectiveCeilingsWallTheirEdge)
{
	SkyTopIn in = SkyCeiling();
	in.frontSky = false;
	SkyWallQuad q;
	EXPECT_FALSE(ComputeSkyTop(in, q));   // [rc4l] an ordinary ceiling
	in.frontPortal = true;
	ASSERT_TRUE(ComputeSkyTop(in, q));
	EXPECT_EQ(q.zbottom[0], 128.f);
	in.samePortal = true;                 // [rc4l] inside one stacked sector
	EXPECT_FALSE(ComputeSkyTop(in, q));
	in.frontReflect = true;
	EXPECT_TRUE(ComputeSkyTop(in, q));
	in.backReflectSameHeight = true;
	EXPECT_FALSE(ComputeSkyTop(in, q));
}

TEST(SkyWall, ASkyFloorFallsFromTheFrontFloor)
{
	SkyWallQuad q;
	ASSERT_TRUE(ComputeSkyBottom(SkyFloor(), q));
	EXPECT_FALSE(q.ceiling);
	EXPECT_EQ(q.ztop[0], -16.f); EXPECT_EQ(q.zbottom[0], -32768.f);
	SkyBottomIn in = SkyFloor();
	in.backNoSkyDraw = true;
	EXPECT_FALSE(ComputeSkyBottom(in, q));
}

TEST(SkyWall, AnUntexturedLowerLetsTheSkyThrough)
{
	SkyBottomIn in = SkyFloor();
	in.bottomNull = true;
	in.backSky = true;
	in.backOpen = true;
	SkyWallQuad q;
	EXPECT_FALSE(ComputeSkyBottom(in, q));
	in.backOpen = false;
	ASSERT_TRUE(ComputeSkyBottom(in, q));
	EXPECT_EQ(q.ztop[0], 0.f);   // [rc4l] up to the back floor, with the sky hack
	EXPECT_TRUE(q.skyhack);
	in.backSky = false;
	in.eyeBelowBackFloor = true;   // [rc4l] Vrack2b
	EXPECT_FALSE(ComputeSkyBottom(in, q));
	in.eyeBelowBackFloor = false;
	ASSERT_TRUE(ComputeSkyBottom(in, q));
	EXPECT_EQ(q.ztop[0], -16.f);
}

TEST(SkyWall, StackedAndReflectiveFloorsWallTheirEdge)
{
	SkyBottomIn in = SkyFloor();
	in.frontSky = false;
	SkyWallQuad q;
	EXPECT_FALSE(ComputeSkyBottom(in, q));
	in.frontPortal = true;
	ASSERT_TRUE(ComputeSkyBottom(in, q));
	EXPECT_EQ(q.ztop[1], -8.f);
	in.samePortal = true;
	EXPECT_FALSE(ComputeSkyBottom(in, q));
	in.frontReflect = true;
	EXPECT_TRUE(ComputeSkyBottom(in, q));
	in.backReflectSameHeight = true;
	EXPECT_FALSE(ComputeSkyBottom(in, q));
}
