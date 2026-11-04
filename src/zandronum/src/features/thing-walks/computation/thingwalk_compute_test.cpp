// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for which movers can skip walking the things around them.
#include "gtest/gtest.h"
#include "features/thing-walks/computation/thingwalk_compute.h"

namespace
{
// [Doruk-Aksoy] A THRUACTORS mover clips against no thing, so its walk over them is skipped; any other mover walks them.
TEST(ComputeMoverPassesThings, OnlyThruActors)
{
	EXPECT_TRUE(ComputeMoverPassesThings(ZX_MF2_THRUACTORS));
	EXPECT_TRUE(ComputeMoverPassesThings(ZX_MF2_THRUACTORS | 1));
	EXPECT_FALSE(ComputeMoverPassesThings(0));
	EXPECT_FALSE(ComputeMoverPassesThings(~ZX_MF2_THRUACTORS));
}
}
