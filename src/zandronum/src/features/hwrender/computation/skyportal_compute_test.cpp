// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/skyportal_compute.h"

using namespace zx::hwrender;

// [rc4l] wdiex02's skybox camera, and the 2048-unit reach fua_dg_skyportal_reach defaults to.
static const float kCamX = 3584.f, kCamY = -2560.f, kReach = 2048.f;

TEST(SkyPortalReach, TheRoomAroundTheCameraIsWithinReach)
{
	EXPECT_FALSE(ComputeBeyondSkyPortalReach(3072.f, 4096.f, -3072.f, -2048.f, kCamX, kCamY, kReach));
}

TEST(SkyPortalReach, AWallOverlappingTheEdgeIsWithinReach)
{
	EXPECT_FALSE(ComputeBeyondSkyPortalReach(5600.f, 9000.f, -2600.f, -2500.f, kCamX, kCamY, kReach));
}

TEST(SkyPortalReach, TheHarbourIsBeyondItOnEverySide)
{
	// [rc4l] The map the room sits beside, which is what the portal used to draw around its camera.
	EXPECT_TRUE(ComputeBeyondSkyPortalReach(-6400.f, -3264.f, -2240.f, -682.f, kCamX, kCamY, kReach));
	EXPECT_TRUE(ComputeBeyondSkyPortalReach(5700.f, 6000.f, -2600.f, -2500.f, kCamX, kCamY, kReach));
	EXPECT_TRUE(ComputeBeyondSkyPortalReach(3500.f, 3600.f, -5000.f, -4700.f, kCamX, kCamY, kReach));
	EXPECT_TRUE(ComputeBeyondSkyPortalReach(3500.f, 3600.f, -400.f, -100.f, kCamX, kCamY, kReach));
}
