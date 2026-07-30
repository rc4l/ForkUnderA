// [rc4l] Tests for the MBF21 FOV cone test. Pinned to DSDA-Doom p_sight.c P_CheckFov, with emphasis
// on the BAM wraparound (the minang>maxang case).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "features/mbf21/computation/fov_compute.h"

using namespace zx::mbf21;

// Standard BAM angle landmarks.
static const uint32_t ANG45  = 0x20000000u;
static const uint32_t ANG90  = 0x40000000u;
static const uint32_t ANG135 = 0x60000000u;
static const uint32_t ANG180 = 0x80000000u;
static const uint32_t ANG270 = 0xC0000000u;
static const uint32_t ANG315 = 0xE0000000u;   // -45 degrees

// ---- cone centred on 0 (straddles the wrap) --------------------------------

TEST(Mbf21Fov, ForwardConeAcceptsAhead)
{
	// facing 0, 90-degree cone => +/-45 degrees around 0.
	EXPECT_TRUE(ComputeInFov(/*dir=*/0, /*facing=*/0, /*fov=*/ANG90));         // dead ahead
	EXPECT_TRUE(ComputeInFov(ANG45, 0, ANG90));                               // right edge (+45)
	EXPECT_TRUE(ComputeInFov(ANG315, 0, ANG90));                              // left edge (-45)
}

TEST(Mbf21Fov, ForwardConeRejectsOutside)
{
	EXPECT_FALSE(ComputeInFov(ANG90, 0, ANG90));    // 90 deg -> outside a +/-45 cone
	EXPECT_FALSE(ComputeInFov(ANG180, 0, ANG90));   // directly behind
	EXPECT_FALSE(ComputeInFov(ANG270, 0, ANG90));
}

// ---- cone centred away from the wrap (normal AND case) ---------------------

TEST(Mbf21Fov, ConeAtNinetyDegrees)
{
	// facing 90, 90-degree cone => [45, 135].
	EXPECT_TRUE(ComputeInFov(ANG90, ANG90, ANG90));    // centre
	EXPECT_TRUE(ComputeInFov(ANG45, ANG90, ANG90));    // low edge
	EXPECT_TRUE(ComputeInFov(ANG135, ANG90, ANG90));   // high edge
	EXPECT_FALSE(ComputeInFov(0, ANG90, ANG90));       // straight ahead is outside
	EXPECT_FALSE(ComputeInFov(ANG180, ANG90, ANG90));
}

// ---- wide / full cones -----------------------------------------------------

TEST(Mbf21Fov, NearFullCircleAcceptsAllButTheAntipode)
{
	// fov 0xFFFFFFFF is one BAM short of a full circle, so the cone [-0x7FFFFFFF, +0x7FFFFFFF]
	// includes everything except the single point exactly 180 degrees behind. (A true 360 = 0 and
	// means "any direction", handled by the codepointer's fov>0 guard, not here.)
	const uint32_t nearFull = 0xFFFFFFFFu;
	EXPECT_TRUE(ComputeInFov(0, 0, nearFull));
	EXPECT_TRUE(ComputeInFov(ANG90, 0, nearFull));
	EXPECT_TRUE(ComputeInFov(ANG270, 0, nearFull));
	EXPECT_TRUE(ComputeInFov(ANG180 - 1, 0, nearFull));   // just before the antipode
	EXPECT_TRUE(ComputeInFov(ANG180 + 1, 0, nearFull));   // just after it
	EXPECT_FALSE(ComputeInFov(ANG180, 0, nearFull));      // the lone excluded BAM
}

TEST(Mbf21Fov, HalfCircleConeIsHalfPlane)
{
	// facing 0, 180-degree cone => the front half-plane [-90, 90].
	EXPECT_TRUE(ComputeInFov(ANG45, 0, ANG180));
	EXPECT_TRUE(ComputeInFov(ANG315, 0, ANG180));
	EXPECT_TRUE(ComputeInFov(ANG90, 0, ANG180));    // exactly +90 is the edge (inclusive)
	EXPECT_FALSE(ComputeInFov(ANG135, 0, ANG180));  // behind the half-plane
}

// ---- cone straddling the wrap but centred off-zero -------------------------

TEST(Mbf21Fov, ConeStraddlingWrapFromNegativeFacing)
{
	// facing -45 (ANG315), 90-degree cone => [-90, 0].
	EXPECT_TRUE(ComputeInFov(ANG315, ANG315, ANG90));   // centre
	EXPECT_TRUE(ComputeInFov(0, ANG315, ANG90));        // high edge at 0
	EXPECT_TRUE(ComputeInFov(ANG270, ANG315, ANG90));   // low edge at -90
	EXPECT_FALSE(ComputeInFov(ANG45, ANG315, ANG90));   // +45 is outside
}
