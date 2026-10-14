// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/levelmesh/computation/wallcache_compute.h"
#include "features/surfaces/walltypes.h"   // [rc4l] RENDERWALL_*

using namespace zx::levelmesh;

static WallCacheStamp Stamp()
{
	WallCacheStamp s;
	s.frontDirty = 7;
	s.backDirty = 3;
	s.sideDirty = 11;
	return s;
}

static WallCacheEligibility Eligible()
{
	WallCacheEligibility e;
	e.isPolyobject = false;
	e.hasHeightsec = false;
	e.hasFFloors = false;
	e.producesPortal = false;
	e.inArea = false;
	return e;
}

// ---- stamp equality --------------------------------------------------------

TEST(WallCache, IdenticalStampsMatch)
{
	EXPECT_TRUE(ComputeStampsEqual(Stamp(), Stamp()));
}

TEST(WallCache, AnyCounterBumpInvalidates)
{
	// A door opening bumps its sector's counter; a switch bumps the side's. Missing any one of the
	// three would leave a wall rendering last frame's geometry.
	{ WallCacheStamp s = Stamp(); s.frontDirty++; EXPECT_FALSE(ComputeStampsEqual(Stamp(), s)); }
	{ WallCacheStamp s = Stamp(); s.backDirty++;  EXPECT_FALSE(ComputeStampsEqual(Stamp(), s)); }
	{ WallCacheStamp s = Stamp(); s.sideDirty++;  EXPECT_FALSE(ComputeStampsEqual(Stamp(), s)); }
}

TEST(WallCache, OneSidedLinesUseZeroForTheBackCounter)
{
	// gl_WallCacheBuildStamp writes 0 for a missing back sector; two one-sided segs must still compare equal.
	WallCacheStamp a = Stamp(); a.backDirty = 0;
	WallCacheStamp b = Stamp(); b.backDirty = 0;
	EXPECT_TRUE(ComputeStampsEqual(a, b));
}

TEST(WallCache, CountersAreComparedNotOrdered)
{
	// The counter only ever has to differ, never to be greater -- it may wrap on a long-running
	// level, and a wrapped-but-different value must still invalidate.
	WallCacheStamp a = Stamp(); a.frontDirty = 2147483647;
	WallCacheStamp b = Stamp(); b.frontDirty = -2147483647 - 1;   // the wrap
	EXPECT_FALSE(ComputeStampsEqual(a, b));
}

// ---- eligibility -----------------------------------------------------------

TEST(WallCache, AnOrdinaryStaticSegIsCacheable)
{
	EXPECT_TRUE(ComputeIsCacheable(Eligible()));
}

TEST(WallCache, EachDisqualifierBlocksCaching)
{
	{ WallCacheEligibility e = Eligible(); e.isPolyobject   = true; EXPECT_FALSE(ComputeIsCacheable(e)); }
	{ WallCacheEligibility e = Eligible(); e.hasHeightsec   = true; EXPECT_FALSE(ComputeIsCacheable(e)); }
	{ WallCacheEligibility e = Eligible(); e.hasFFloors     = true; EXPECT_FALSE(ComputeIsCacheable(e)); }
	{ WallCacheEligibility e = Eligible(); e.producesPortal = true; EXPECT_FALSE(ComputeIsCacheable(e)); }
	{ WallCacheEligibility e = Eligible(); e.inArea         = true; EXPECT_FALSE(ComputeIsCacheable(e)); }
}

// ---- the combined reuse decision -------------------------------------------

TEST(WallCache, ReuseNeedsEligibilityCacheAndAMatchingStamp)
{
	const WallCacheStamp s = Stamp();
	EXPECT_TRUE(ComputeCanReuse(Eligible(), true, s, s));
}

TEST(WallCache, NothingCachedYetMeansNoReuse)
{
	const WallCacheStamp s = Stamp();
	EXPECT_FALSE(ComputeCanReuse(Eligible(), false, s, s));
}

TEST(WallCache, IneligibleSegNeverReusesEvenWithAMatchingStamp)
{
	// A view-dependent seg can produce identical inputs and still need regenerating, because what
	// changed is the viewpoint rather than the map.
	WallCacheEligibility e = Eligible();
	e.hasHeightsec = true;
	const WallCacheStamp s = Stamp();
	EXPECT_FALSE(ComputeCanReuse(e, true, s, s));
}

// [rc4l] WDI01's bathroom mirror: the map bake gave the mirror line its wall, which hid the mirror drawn on the same plane.
TEST(WallCache, AOneSidedMirrorLineIsAMirrorNotAWall)
{
	EXPECT_TRUE(ComputeIsMirrorWall(kLineMirror, true, true));
	EXPECT_FALSE(ComputeIsMirrorWall(kLineMirror, false, true));   // [rc4l] GL mirrors only a one-sided middle
	EXPECT_FALSE(ComputeIsMirrorWall(kLineMirror, true, false));   // [rc4l] r_mirrors off draws the wall
	EXPECT_FALSE(ComputeIsMirrorWall(0, true, true));
}

TEST(WallCache, ChangedStampBlocksReuse)
{
	WallCacheStamp cur = Stamp();
	cur.frontDirty++;   // a door coming down bumps its sector's counter
	EXPECT_FALSE(ComputeCanReuse(Eligible(), true, Stamp(), cur));
}

// [rc4l] WDI06's window is an upper texture, RWIND8: GL draws it unmasked, so its see-through texels come out black rather than showing the room behind.
TEST(WallCache, UpperLowerAndOneSidedMiddlesAreDrawnUnmasked)
{
	for (int t : { kRenderWallTop, kRenderWallM1S, kRenderWallBottom, kRenderWallMirrorSurface })
		EXPECT_TRUE(ComputeWallPlainPass(t)) << t;
	// [rc4l] A two-sided middle, its no-fog twin, a 3D floor's side, sky and portals are all something else.
	for (int t : { 0, 3, 5, 6, 7, 11, 13, 15, 16 })
		EXPECT_FALSE(ComputeWallPlainPass(t)) << t;
}

// [rc4l] WDI09's slime floor scrolled every tic and no counter the flat bake read ever moved, so it froze at its first offset.
TEST(WallCache, ACounterReadsChangedOnceEachTimeItMoves)
{
	int seen = 4;
	EXPECT_FALSE(ComputeTakeChange(seen, 4));
	EXPECT_TRUE(ComputeTakeChange(seen, 5));
	EXPECT_EQ(5, seen);
	EXPECT_FALSE(ComputeTakeChange(seen, 5));
}

// [rc4l] WDI19's pool: a script tinted the water's control sector blue after the bake, and no change the sweep compared showed it, so the floor under the water stayed grey.
TEST(WallCache, ASectorsRecolourIsARelightAndItsMoveIsARebake)
{
	int mapA = 0, mapB = 0;
	const SectorLook was = { 160, 100, 200, 7, 8, &mapA };
	SectorLook now = was;
	EXPECT_EQ(kSectorSame, ComputeSectorRebake(was, now));
	now.colormap = &mapB;
	EXPECT_EQ(kSectorRelit, ComputeSectorRebake(was, now));
	now = was; now.light = 96;
	EXPECT_EQ(kSectorRelit, ComputeSectorRebake(was, now));
	now = was; now.ceilPic = 9;
	EXPECT_EQ(kSectorRetextured, ComputeSectorRebake(was, now));
	now = was; now.floorPic = 9; now.light = 96;
	EXPECT_EQ(kSectorRetextured, ComputeSectorRebake(was, now));  // [rc4l] the stronger need wins
	now = was; now.ceilD = 210;
	EXPECT_EQ(kSectorMoved, ComputeSectorRebake(was, now));
	now = was; now.floorD = 90; now.floorPic = 9;
	EXPECT_EQ(kSectorMoved, ComputeSectorRebake(was, now));
}

// [rc4l] A plain sidedef: STARTAN2 on every part, no offset, unit scale.
static SideLook PlainSide()
{
	SideLook s;
	for (int p = 0; p < 3; p++) { s.tex[p] = 7; s.xoff[p] = s.yoff[p] = 0; s.xscale[p] = s.yscale[p] = 65536; }
	s.light = 0; s.flags = 0;
	return s;
}

// [rc4l] AOW03's 101 scrolling walls were baked again every frame while only their offsets moved.
TEST(WallCache, AnOffsetAloneIsAScrollOfThePiecesAlreadyBaked)
{
	const SideLook baked = PlainSide();
	EXPECT_EQ(kSideSame, ComputeSideChange(baked, baked));
	for (int p = 0; p < 3; p++)
	{
		SideLook across = baked; across.xoff[p] = 8 * 65536;
		EXPECT_EQ(kSideScrolled, ComputeSideChange(baked, across));
		SideLook down = baked; down.yoff[p] = -3 * 65536;
		EXPECT_EQ(kSideScrolled, ComputeSideChange(baked, down));
	}
}

TEST(WallCache, ANewTextureOrScaleOnAnyPartIsABake)
{
	const SideLook baked = PlainSide();
	for (int p = 0; p < 3; p++)
	{
		SideLook switched = baked; switched.tex[p] = 9; switched.xoff[0] = 65536;
		EXPECT_EQ(kSideRebuild, ComputeSideChange(baked, switched));
		SideLook wider = baked; wider.xscale[p] = 2 * 65536;
		EXPECT_EQ(kSideRebuild, ComputeSideChange(baked, wider));
		SideLook taller = baked; taller.yscale[p] = 32768;
		EXPECT_EQ(kSideRebuild, ComputeSideChange(baked, taller));
	}
}

// [rc4l] walllight: a Transfer_WallLight change marks the sidedef like a scroll does, and taking it for a scroll left the wall unlit.
TEST(WallCache, ASidesOwnLightOrFlagsChangingIsABake)
{
	const SideLook baked = PlainSide();
	SideLook lit = baked; lit.light = 64;
	EXPECT_EQ(kSideRebuild, ComputeSideChange(baked, lit));
	SideLook flagged = baked; flagged.flags = 4; flagged.xoff[1] = 65536;
	EXPECT_EQ(kSideRebuild, ComputeSideChange(baked, flagged));
}

TEST(WallCache, AScrollPastTheLimitGoesBackIntoTheVertices)
{
	EXPECT_TRUE(ComputeScrollInRange(0.f, 0.f));
	EXPECT_TRUE(ComputeScrollInRange(kMaxScrollShift, -kMaxScrollShift));
	EXPECT_FALSE(ComputeScrollInRange(kMaxScrollShift + 1.f, 0.f));
	EXPECT_FALSE(ComputeScrollInRange(-kMaxScrollShift - 1.f, 0.f));
	EXPECT_FALSE(ComputeScrollInRange(0.f, kMaxScrollShift + 1.f));
	EXPECT_FALSE(ComputeScrollInRange(0.f, -kMaxScrollShift - 1.f));
}

// [rc4l] The bake, its reshade and the decals all took a middle's kind from one rule spelled three times.
TEST(WallCacheCompute, APartIsDrawnAsItsKindOfWallPiece)
{
	EXPECT_EQ(RENDERWALL_TOP, ComputePartRenderType(kPartUpper, true));
	EXPECT_EQ(RENDERWALL_BOTTOM, ComputePartRenderType(kPartLower, false));
	EXPECT_EQ(RENDERWALL_M2S, ComputePartRenderType(kPartMiddle, true));
	EXPECT_EQ(RENDERWALL_M1S, ComputePartRenderType(kPartMiddle, false));
	EXPECT_EQ(RENDERWALL_NONE, ComputePartRenderType(kWallParts, true));
	EXPECT_EQ(RENDERWALL_NONE, ComputePartRenderType(-1, false));
}
