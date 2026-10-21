// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/decals/computation/markrules_compute.h"

#include <gtest/gtest.h>
#include <string>

using namespace zx::decals;

namespace {

MarkSurface Wall() { MarkSurface s = { kSurfaceWall, false, false, false, false, false }; return s; }
MarkSurface Floor() { MarkSurface s = { kSurfaceFloor, false, false, false, false, false }; return s; }
const MarkSettings kDefaults = { 1024, true, true, false };
const MarkRequest kShot = { kSourceHitscan, false };

} // namespace

TEST(MarkRules, ImpactsAreTheShotsMissilesAndBlood)
{
	EXPECT_TRUE(ComputeIsImpact(kSourceHitscan));
	EXPECT_TRUE(ComputeIsImpact(kSourceRail));
	EXPECT_TRUE(ComputeIsImpact(kSourceMissile));
	EXPECT_TRUE(ComputeIsImpact(kSourceBlood));
	EXPECT_FALSE(ComputeIsImpact(kSourcePlaced));
	EXPECT_FALSE(ComputeIsImpact(kSourceScript));
	EXPECT_FALSE(ComputeIsImpact(kSourceSpray));
}

TEST(MarkRules, APlainSurfaceTakesAShot)
{
	EXPECT_EQ(ComputeMarkAllowed(Wall(), kShot, kDefaults), kMarkAllowed);
	EXPECT_EQ(ComputeMarkAllowed(Floor(), kShot, kDefaults), kMarkAllowed);
}

TEST(MarkRules, ASurfaceRefusesBySideHorizonSkyLiquidAndTextureInThatOrder)
{
	MarkSurface s = Wall();
	s.sideOptOut = s.horizon = s.textureRefuses = true;
	EXPECT_EQ(ComputeSurfaceVerdict(s), kMarkSideOptOut);
	s.sideOptOut = false;
	EXPECT_EQ(ComputeSurfaceVerdict(s), kMarkHorizon);
	s.horizon = false;
	EXPECT_EQ(ComputeSurfaceVerdict(s), kMarkTextureRefuses);
	MarkSurface f = Floor();
	f.sideOptOut = f.horizon = true;   // [rc4l] a flat has no side and no horizon line to refuse by
	EXPECT_EQ(ComputeSurfaceVerdict(f), kMarkAllowed);
	f.sky = f.liquid = f.textureRefuses = true;
	EXPECT_EQ(ComputeSurfaceVerdict(f), kMarkSky);
	f.sky = false;
	EXPECT_EQ(ComputeSurfaceVerdict(f), kMarkLiquid);
	f.liquid = false;
	EXPECT_EQ(ComputeSurfaceVerdict(f), kMarkTextureRefuses);
}

TEST(MarkRules, AWallRefusesItsOpenSpanAndAFlatItsPortalTranslucentAndScrollingPlanes)
{
	MarkSurface w = Wall();
	w.open = true;
	EXPECT_EQ(ComputeSurfaceVerdict(w), kMarkOpenSpan);
	w.open = false;
	w.portal = w.scrolls = true;   // [rc4l] a wall's scrolling is its side's noautodecals, set by the scroller
	EXPECT_EQ(ComputeSurfaceVerdict(w), kMarkAllowed);
	MarkSurface f = Floor();
	f.open = true;   // [rc4l] a flat always has its own texture
	EXPECT_EQ(ComputeSurfaceVerdict(f), kMarkAllowed);
	f.portal = f.translucent = f.scrolls = f.textureRefuses = true;
	EXPECT_EQ(ComputeSurfaceVerdict(f), kMarkPortal);
	f.portal = false;
	EXPECT_EQ(ComputeSurfaceVerdict(f), kMarkTranslucent);
	f.translucent = false;
	EXPECT_EQ(ComputeSurfaceVerdict(f), kMarkScrolls);
	f.scrolls = false;
	EXPECT_EQ(ComputeSurfaceVerdict(f), kMarkTextureRefuses);
}

TEST(MarkRules, ASeeThroughSlabsSideRefusesAWallMarkAsItsPlanesRefuseAFlatOne)
{
	MarkSurface w = Wall();
	w.translucent = w.textureRefuses = true;
	EXPECT_EQ(ComputeSurfaceVerdict(w), kMarkTranslucent);
	w.sideOptOut = true;   // [rc4l] the side's own opt-out is judged first
	EXPECT_EQ(ComputeSurfaceVerdict(w), kMarkSideOptOut);
	const MarkSettings settings = { 1024, true, true, false };
	const MarkRequest placed = { kSourcePlaced, true };
	EXPECT_EQ(ComputeMarkAllowed(w, placed, settings), kMarkAllowed);   // [rc4l] a permanent decal sticks wherever its trace stopped
}

TEST(MarkRules, AServerImpactAndAMissileWithMissileDecalsOffAreRefusedBeforeTheirChoice)
{
	const MarkSettings server = { 1024, true, true, true };
	const MarkSettings noMissiles = { 1024, false, true, false };
	const MarkRequest missile = { kSourceMissile, false };
	const MarkRequest script = { kSourceScript, false };
	EXPECT_TRUE(ComputeRefusedBeforeChoice(kShot, server));
	EXPECT_FALSE(ComputeRefusedBeforeChoice(script, server));
	EXPECT_TRUE(ComputeRefusedBeforeChoice(missile, noMissiles));
	EXPECT_FALSE(ComputeRefusedBeforeChoice(kShot, noMissiles));
	EXPECT_FALSE(ComputeRefusedBeforeChoice(missile, kDefaults));
	const MarkSettings off = { 0, true, true, false };
	const MarkRequest blood = { kSourceBlood, false };
	const MarkRequest spray = { kSourceSpray, false };
	EXPECT_TRUE(ComputeRefusedBeforeChoice(blood, off));
	EXPECT_TRUE(ComputeRefusedBeforeChoice(spray, off));
	EXPECT_FALSE(ComputeRefusedBeforeChoice(kShot, off));   // [rc4l] a shot's generator is chosen first, then refused
	EXPECT_FALSE(ComputeRefusedBeforeChoice(blood, kDefaults));
}

TEST(MarkRules, AServerMakesNoImpactButStillPlacesTheMapsAndScriptsDecals)
{
	const MarkSettings server = { 1024, true, true, true };
	EXPECT_EQ(ComputeMarkAllowed(Floor(), kShot, server), kMarkServer);
	const MarkRequest script = { kSourceScript, false };
	EXPECT_EQ(ComputeMarkAllowed(Wall(), script, server), kMarkAllowed);
	const MarkRequest placed = { kSourcePlaced, true };
	EXPECT_EQ(ComputeMarkAllowed(Wall(), placed, server), kMarkAllowed);
}

TEST(MarkRules, DecalsOffRefusesEveryMarkButAPermanentOne)
{
	const MarkSettings off = { 0, true, true, false };
	EXPECT_EQ(ComputeMarkAllowed(Wall(), kShot, off), kMarkDecalsOff);
	EXPECT_EQ(ComputeMarkAllowed(Floor(), kShot, off), kMarkDecalsOff);
	MarkSurface refusing = Wall();
	refusing.textureRefuses = refusing.sideOptOut = true;
	const MarkRequest placed = { kSourcePlaced, true };
	EXPECT_EQ(ComputeMarkAllowed(refusing, placed, off), kMarkAllowed);   // [rc4l] stock's map-placed decal ignores all three
}

TEST(MarkRules, MissileDecalsOffRefusesOnlyMissiles)
{
	const MarkSettings noMissiles = { 1024, false, true, false };
	const MarkRequest missile = { kSourceMissile, false };
	EXPECT_EQ(ComputeMarkAllowed(Wall(), missile, noMissiles), kMarkMissileDecalsOff);
	EXPECT_EQ(ComputeMarkAllowed(Wall(), kShot, noMissiles), kMarkAllowed);
}

TEST(MarkRules, FlatMarksOffRefusesOnlyFloorsAndCeilings)
{
	const MarkSettings noFlats = { 1024, true, false, false };
	EXPECT_EQ(ComputeMarkAllowed(Floor(), kShot, noFlats), kMarkFlatsOff);
	MarkSurface ceiling = Floor();
	ceiling.kind = kSurfaceCeiling;
	EXPECT_EQ(ComputeMarkAllowed(ceiling, kShot, noFlats), kMarkFlatsOff);
	EXPECT_EQ(ComputeMarkAllowed(Wall(), kShot, noFlats), kMarkAllowed);
}

TEST(MarkRules, OnlyAnImpactIsRefusedByAHorizonLine)
{
	MarkSurface horizon = Wall();
	horizon.horizon = true;
	EXPECT_EQ(ComputeMarkAllowed(horizon, kShot, kDefaults), kMarkHorizon);
	const MarkRequest spray = { kSourceSpray, false };
	EXPECT_EQ(ComputeMarkAllowed(horizon, spray, kDefaults), kMarkAllowed);
	horizon.sideOptOut = true;
	EXPECT_EQ(ComputeMarkAllowed(horizon, spray, kDefaults), kMarkSideOptOut);
}

TEST(MarkRules, EveryVerdictHasAName)
{
	for (int v = 0; v < kMarkVerdicts; v++) EXPECT_STRNE(ComputeMarkVerdictName(v), "?") << v;
	EXPECT_EQ(std::string(ComputeMarkVerdictName(kMarkLiquid)), "liquid");
	EXPECT_STREQ(ComputeMarkVerdictName(-1), "?");
	EXPECT_STREQ(ComputeMarkVerdictName(kMarkVerdicts), "?");
}

TEST(MarkRules, AMissileTouchesTheCeilingFirstThenTheFloorWithinTheSlack)
{
	const int slack = 4, floorz = 0, ceilingz = 128;
	EXPECT_EQ(ComputeMissilePlaneHit(2, 8, floorz, ceilingz, slack), kSurfaceFloor);
	EXPECT_EQ(ComputeMissilePlaneHit(117, 8, floorz, ceilingz, slack), kSurfaceCeiling);
	EXPECT_EQ(ComputeMissilePlaneHit(64, 8, floorz, ceilingz, slack), kSurfaceWall);
	EXPECT_EQ(ComputeMissilePlaneHit(0, 6, 0, 8, slack), kSurfaceCeiling);   // [rc4l] a sliver of a sector touches both
}

TEST(MarkRules, ThePlaneOwnerIsTheNearestHeightWithinTheSlack)
{
	const long long heights[] = { 64, 130, 126 };
	EXPECT_EQ(ComputePlaneOwner(127, heights, 3, 4), 2);   // [rc4l] 126 is nearer than 130
	EXPECT_EQ(ComputePlaneOwner(128, heights, 3, 4), 1);   // [rc4l] a tie keeps the first
	EXPECT_EQ(ComputePlaneOwner(129, heights, 3, 4), 1);
	EXPECT_EQ(ComputePlaneOwner(64, heights, 3, 0), 0);
	EXPECT_EQ(ComputePlaneOwner(200, heights, 3, 4), -1);
	EXPECT_EQ(ComputePlaneOwner(64, heights, 0, 4), -1);
}

TEST(MarkRules, TheBudgetRetiresTheOldestToMakeRoom)
{
	EXPECT_EQ(ComputeMarksToEvict(10, 1024), 0);
	EXPECT_EQ(ComputeMarksToEvict(1024, 1024), 1);
	EXPECT_EQ(ComputeMarksToEvict(1030, 1024), 7);   // [rc4l] a lowered cl_maxdecals sheds the extra at once
	EXPECT_EQ(ComputeMarksToEvict(5, 0), 5);
}

TEST(MarkRules, AGroupsChoiceDrawsTheGameStreamOnlyWhereShippedBuildsDid)
{
	EXPECT_TRUE(ComputeChoiceFromGameStream(kSourceBlood, kSurfaceWall));
	EXPECT_TRUE(ComputeChoiceFromGameStream(kSourceHitscan, kSurfaceFloor));
	EXPECT_TRUE(ComputeChoiceFromGameStream(kSourceMissile, kSurfaceCeiling));
	EXPECT_FALSE(ComputeChoiceFromGameStream(kSourceBlood, kSurfaceFloor));
	EXPECT_FALSE(ComputeChoiceFromGameStream(kSourceRail, kSurfaceFloor));
}
