// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "computation/demobytes_compute.h"

TEST(DemoBytes, EachStringTakesItsNullTerminator)
{
	EXPECT_EQ(zx::ComputeDemoBytes(2, { 5 }), 8u);
	EXPECT_EQ(zx::ComputeDemoBytes(2, {}), 2u);
}

TEST(DemoBytes, TheUserinfoRecordIsTheTwentyTwoUpstreamCountsPlusItsStrings)
{
	// [rc4l] A header, gender, colour set, colour, aim distance, rail colour, handicap, tics per update, connection type and client flags.
	const size_t fixed = 1 + 1 + 1 + 4 + 4 + 4 + 1 + 1 + 1 + 1;
	EXPECT_EQ(zx::ComputeDemoBytes(fixed, { 0, 0, 0 }), 22u);
	EXPECT_EQ(zx::ComputeDemoBytes(fixed, { 6, 4, 7 }), 22u + 6 + 4 + 7);
}

TEST(DemoBytes, ALocalCommandsArgumentIsTheThreeUpstreamAddsToIt)
{
	EXPECT_EQ(zx::ComputeDemoBytes(2, { 10 }), 10u + 3);
}
