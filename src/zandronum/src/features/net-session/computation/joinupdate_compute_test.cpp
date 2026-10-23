// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-session/computation/joinupdate_compute.h"

#include <gtest/gtest.h>

using namespace zx::netsession;

namespace {
const int64_t kUnit = 1 << 16;
}

TEST(ComputeSpawnRoundsPosition, WholeUnitsArriveWhereTheyAre)
{
	EXPECT_FALSE(ComputeSpawnRoundsPosition(0, 0, 0));
	EXPECT_FALSE(ComputeSpawnRoundsPosition(512 * kUnit, -96 * kUnit, 24 * kUnit));
}

TEST(ComputeSpawnRoundsPosition, AFractionOnAnyAxisWouldMoveIt)
{
	// [rc4l] netsync's dropped clip lay 1.22 units from where a late joiner drew it.
	EXPECT_TRUE(ComputeSpawnRoundsPosition(512 * kUnit + 14418, 0, 0));
	EXPECT_TRUE(ComputeSpawnRoundsPosition(0, -96 * kUnit - 1, 0));
	EXPECT_TRUE(ComputeSpawnRoundsPosition(0, 0, kUnit / 2));
}

TEST(ComputeSpawnRoundsPosition, AWholeUnitBeyondAShortWouldWrap)
{
	EXPECT_FALSE(ComputeSpawnRoundsPosition(32767 * kUnit, -32768 * kUnit, 0));
	EXPECT_TRUE(ComputeSpawnRoundsPosition(40000 * kUnit, 0, 0));
	EXPECT_TRUE(ComputeSpawnRoundsPosition(0, -40000 * kUnit, 0));
}
