// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/debugorbit_compute.h"

#include <gtest/gtest.h>

using namespace zx::frame;

TEST(DebugOrbit, TheCircleStartsWhereTheAimWasAndComesBackRound)
{
	double y, p;
	ComputeOrbitOffset(0.0, 10.0, 2.0, y, p);
	EXPECT_NEAR(0.0, y, 1e-9);
	EXPECT_NEAR(0.0, p, 1e-9);
	ComputeOrbitOffset(0.5, 10.0, 2.0, y, p);   // [rc4l] a quarter round
	EXPECT_NEAR(10.0, y, 1e-9);
	EXPECT_NEAR(10.0, p, 1e-9);
	ComputeOrbitOffset(1.0, 10.0, 2.0, y, p);   // [rc4l] half round, the far side
	EXPECT_NEAR(20.0, y, 1e-9);
	EXPECT_NEAR(0.0, p, 1e-9);
	ComputeOrbitOffset(2.0, 10.0, 2.0, y, p);
	EXPECT_NEAR(0.0, y, 1e-9);
	EXPECT_NEAR(0.0, p, 1e-9);
}

TEST(DebugOrbit, NoPeriodHoldsStill)
{
	double y, p;
	ComputeOrbitOffset(3.0, 10.0, 0.0, y, p);
	EXPECT_DOUBLE_EQ(0.0, y);
	EXPECT_DOUBLE_EQ(0.0, p);
}

// [rc4l] Units are owed against what was applied, so rounding never drifts the circle off where it started.
TEST(DebugOrbit, UnitsAreWhatIsOwedSoRoundingNeverDrifts)
{
	EXPECT_EQ(182, ComputeOrbitUnits(1.0, 0));   // [rc4l] 65536 / 360 = 182.04
	EXPECT_EQ(0, ComputeOrbitUnits(1.0, 182));
	EXPECT_EQ(-182, ComputeOrbitUnits(0.0, 182));
}
