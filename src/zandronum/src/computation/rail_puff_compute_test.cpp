// [rc4l] Tests for the rail-endpoint spawn decision (rail_puff_compute).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "computation/rail_puff_compute.h"

TEST(RailPuff, WallSpawnsPuffAndDecalNoSkyGuard)
{
	const RailEndpointSpawn s = ComputeRailEndpointSpawn(ZX_TRACE_HitWall);
	EXPECT_TRUE(s.puff);
	EXPECT_TRUE(s.decal);
	EXPECT_FALSE(s.skyGuard); // walls don't need the sky-flat guard
}

// [rc4l] The change: floor/ceiling hits now spawn the puff (they didn't before) -- but no decal
// (flats don't take wall decals) -- and the caller must apply the sky-flat guard.
TEST(RailPuff, FloorAndCeilingSpawnPuffNoDecalWithSkyGuard)
{
	for (int hit : {ZX_TRACE_HitFloor, ZX_TRACE_HitCeiling})
	{
		const RailEndpointSpawn s = ComputeRailEndpointSpawn(hit);
		EXPECT_TRUE(s.puff) << "floor/ceiling should now spawn a rail puff";
		EXPECT_FALSE(s.decal) << "flats must not get a wall decal";
		EXPECT_TRUE(s.skyGuard) << "floor/ceiling must guard against a sky flat";
	}
}

TEST(RailPuff, NoneAndActorSpawnNeither)
{
	for (int hit : {ZX_TRACE_HitNone, ZX_TRACE_HitActor})
	{
		const RailEndpointSpawn s = ComputeRailEndpointSpawn(hit);
		EXPECT_FALSE(s.puff);
		EXPECT_FALSE(s.decal);
		EXPECT_FALSE(s.skyGuard);
	}
}
