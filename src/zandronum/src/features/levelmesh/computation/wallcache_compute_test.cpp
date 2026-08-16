// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/levelmesh/computation/wallcache_compute.h"

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
	// BuildStamp writes 0 for a missing back sector; two one-sided segs must still compare equal.
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

TEST(WallCache, ChangedStampBlocksReuse)
{
	WallCacheStamp cur = Stamp();
	cur.frontDirty++;   // a door coming down bumps its sector's counter
	EXPECT_FALSE(ComputeCanReuse(Eligible(), true, Stamp(), cur));
}
