// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/framephase/computation/framephase_compute.h"

#include <gtest/gtest.h>
#include <string.h>

using namespace zx::framephase;

TEST(FramePhase, EachPhaseHasItsNameAndAnythingElseIsUnknown)
{
	EXPECT_STREQ("sim", ComputePhaseName(kPhaseSim));
	EXPECT_STREQ("present", ComputePhaseName(kPhasePresent));
	EXPECT_STREQ("input", ComputePhaseName(kPhaseInput));
	EXPECT_STREQ("display", ComputePhaseName(kPhaseDisplay));
	EXPECT_STREQ("flip", ComputePhaseName(kPhaseFlip));
	EXPECT_STREQ("?", ComputePhaseName(kPhaseCount));
	EXPECT_STREQ("?", ComputePhaseName(-1));
}
