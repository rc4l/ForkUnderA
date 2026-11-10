// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/net-client/computation/serverfloor_compute.h"

#include <cstdint>

#include <gtest/gtest.h>

using namespace zx::netclient;

namespace {
const int64_t UNIT = 65536;   // [Doruk-Aksoy] One map unit in 48.16 fixed point.
}

// [Doruk-Aksoy] Standing on the floor under its box, or above it and falling, the actor keeps that floor.
TEST(ServerHeldFloorZ, AFloorAtOrBelowTheServersHeightIsKept)
{
	EXPECT_EQ(-64 * UNIT, ComputeServerHeldFloorZ(-64 * UNIT, -64 * UNIT, false));
	EXPECT_EQ(-64 * UNIT, ComputeServerHeldFloorZ(-64 * UNIT, 0, false));
}

// [Doruk-Aksoy] A dead Arachnotron the server held at -64 on narrow steps fell to the hall's -128 on every client when its box, a unit off by rounding, reached a step above it.
TEST(ServerHeldFloorZ, AFloorAboveTheServersHeightBecomesThatHeight)
{
	EXPECT_EQ(-64 * UNIT, ComputeServerHeldFloorZ(-48 * UNIT, -64 * UNIT, false));
	EXPECT_EQ(-1, ComputeServerHeldFloorZ(0, -1, false));
}

// [Doruk-Aksoy] A Mancubus touching an Arachnotron on a client stopped the floor check before any step was read, and stood on the floor under its centre, 48 below the server's.
TEST(ServerHeldFloorZ, AFloorCheckStoppedByAnActorIsReadAgainFromTheLines)
{
	EXPECT_TRUE(ComputeRereadFloorFromLines(false, false));
	EXPECT_FALSE(ComputeRereadFloorFromLines(true, false));
}

// [Doruk-Aksoy] The console player keeps the floor its prediction worked out, blocked or not.
TEST(ServerHeldFloorZ, TheConsolePlayerIsNeverReadAgain)
{
	EXPECT_FALSE(ComputeRereadFloorFromLines(false, true));
	EXPECT_FALSE(ComputeRereadFloorFromLines(true, true));
}

// [Doruk-Aksoy] The console player's own floor is the box's whatever its height, as its prediction needs.
TEST(ServerHeldFloorZ, TheConsolePlayerKeepsTheBoxFloor)
{
	EXPECT_EQ(-48 * UNIT, ComputeServerHeldFloorZ(-48 * UNIT, -64 * UNIT, true));
	EXPECT_EQ(-64 * UNIT, ComputeServerHeldFloorZ(-64 * UNIT, 0, true));
}
