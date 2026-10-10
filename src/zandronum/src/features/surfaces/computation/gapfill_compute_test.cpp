// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/surfaces/computation/gapfill_compute.h"

using namespace zx::surfaces;

namespace {

GapInputs Plain(bool top)
{
	GapInputs in = {};
	in.top = top;
	in.hasPartner = true;
	return in;
}

} // namespace

// [rc4l] wdiexsec: a flat step with no lower texture is flooded by GL, and the bake left it a hole.
TEST(GapFill, AFlatStepWithNoTextureIsFlooded)
{
	EXPECT_EQ(kGapFlood, ComputeGapFill(Plain(false)));
	EXPECT_EQ(kGapFlood, ComputeGapFill(Plain(true)));
}

TEST(GapFill, ASlopedStepIsDrawnWithTheFrontFlatUnlessASkyIsInvolved)
{
	GapInputs in = Plain(false);
	in.sloped = true;
	EXPECT_EQ(kGapFrontFlat, ComputeGapFill(in));
	in.frontSky = true;
	EXPECT_EQ(kGapFlood, ComputeGapFill(in));
	in.frontSky = false;
	in.backSky = true;
	EXPECT_EQ(kGapNone, ComputeGapFill(in));
}

TEST(GapFill, WhatGLLeavesAloneIsLeftAlone)
{
	bool GapInputs::* const skip[] = { &GapInputs::backSky, &GapInputs::backPortal, &GapInputs::transdoor,
		&GapInputs::polyobj, &GapInputs::fakeFlat };
	for (bool GapInputs::*field : skip)
	{
		GapInputs in = Plain(false);
		in.*field = true;
		EXPECT_EQ(kGapNone, ComputeGapFill(in));
	}
	GapInputs orphan = Plain(false);
	orphan.hasPartner = false;
	EXPECT_EQ(kGapNone, ComputeGapFill(orphan));
}

// [rc4l] An FF_FIX 3D floor abuses a missing LOWER texture on purpose, so only a lower gap backs off for it.
TEST(GapFill, AFixedThreeDFloorStopsOnlyALowerFlood)
{
	GapInputs lower = Plain(false), upper = Plain(true);
	lower.fixAbuse = upper.fixAbuse = true;
	EXPECT_EQ(kGapNone, ComputeGapFill(lower));
	EXPECT_EQ(kGapFlood, ComputeGapFill(upper));
}
