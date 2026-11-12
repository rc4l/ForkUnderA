// [rc4l] Tests for the rail-endpoint spawn decision (rail_puff_compute).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "computation/rail_puff_compute.h"

TEST(RailPuff, WallSpawnsPuffAndDecal)
{
	const RailEndpointSpawn s = ComputeRailEndpointSpawn(ZX_TRACE_HitWall);
	EXPECT_TRUE(s.puff);
	EXPECT_TRUE(s.decal);
}

// [rc4l] The change: floor/ceiling hits now spawn the puff (they didn't before) and the mark, which
// features/decals judges like a shot's.
TEST(RailPuff, FloorAndCeilingSpawnPuffAndMark)
{
	for (int hit : {ZX_TRACE_HitFloor, ZX_TRACE_HitCeiling})
	{
		const RailEndpointSpawn s = ComputeRailEndpointSpawn(hit);
		EXPECT_TRUE(s.puff) << "floor/ceiling should now spawn a rail puff";
		EXPECT_TRUE(s.decal) << "a flat takes the mark a shot's does";
	}
}

TEST(RailPuff, NoneAndActorSpawnNeither)
{
	for (int hit : {ZX_TRACE_HitNone, ZX_TRACE_HitActor})
	{
		const RailEndpointSpawn s = ComputeRailEndpointSpawn(hit);
		EXPECT_FALSE(s.puff);
		EXPECT_FALSE(s.decal);
	}
}

// [rc4l] zandronum@2e629a9b7b96 adds the horizon line: a wall puff on one went into the sky as a sky flat's does.
TEST(RailPuff, APuffInTheSkyGoesUnlessItExplodesThere)
{
	EXPECT_TRUE(ComputeRailPuffInSky(ZX_TRACE_HitWall, true, false, false, false));
	EXPECT_FALSE(ComputeRailPuffInSky(ZX_TRACE_HitWall, false, true, true, false)) << "a wall's sectors' skies do not matter";
	EXPECT_TRUE(ComputeRailPuffInSky(ZX_TRACE_HitFloor, false, true, false, false));
	EXPECT_FALSE(ComputeRailPuffInSky(ZX_TRACE_HitFloor, true, false, true, false));
	EXPECT_TRUE(ComputeRailPuffInSky(ZX_TRACE_HitCeiling, false, false, true, false));
	EXPECT_FALSE(ComputeRailPuffInSky(ZX_TRACE_HitCeiling, false, true, false, false));
	EXPECT_FALSE(ComputeRailPuffInSky(ZX_TRACE_HitWall, true, true, true, true)) << "SKYEXPLODE keeps it";
	EXPECT_FALSE(ComputeRailPuffInSky(ZX_TRACE_HitActor, true, true, true, false));
}
