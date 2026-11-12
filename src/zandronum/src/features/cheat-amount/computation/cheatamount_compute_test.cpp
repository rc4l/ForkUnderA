// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/cheat-amount/computation/cheatamount_compute.h"

#include <climits>
#include <gtest/gtest.h>

using namespace zx::cheatamount;

TEST(CheatAmount, NoArgumentIsZero)
{
	EXPECT_EQ(ComputeCheatAmountArg(nullptr), 0);
}

TEST(CheatAmount, AmountsPastTheOldCapsPassThrough)
{
	EXPECT_EQ(ComputeCheatAmountArg("1"), 1);
	EXPECT_EQ(ComputeCheatAmountArg("255"), 255);
	EXPECT_EQ(ComputeCheatAmountArg("256"), 256);
	EXPECT_EQ(ComputeCheatAmountArg("32767"), 32767);
	EXPECT_EQ(ComputeCheatAmountArg("32768"), 32768);
	EXPECT_EQ(ComputeCheatAmountArg("100000"), 100000);
	EXPECT_EQ(ComputeCheatAmountArg("2147483647"), INT_MAX);
}

TEST(CheatAmount, BelowOneIsOne)
{
	EXPECT_EQ(ComputeCheatAmountArg("0"), 1);
	EXPECT_EQ(ComputeCheatAmountArg("-5"), 1);
	EXPECT_EQ(ComputeCheatAmountArg("-99999999999999999999"), 1);
	EXPECT_EQ(ComputeCheatAmountArg("abc"), 1);
	EXPECT_EQ(ComputeCheatAmountArg(""), 1);
}

TEST(CheatAmount, AboveIntMaxIsIntMax)
{
	EXPECT_EQ(ComputeCheatAmountArg("2147483648"), INT_MAX);
	EXPECT_EQ(ComputeCheatAmountArg("99999999999999999999999"), INT_MAX);
}

TEST(CheatAmount, LeadingSpaceAndTrailingTextReadLikeAtoi)
{
	EXPECT_EQ(ComputeCheatAmountArg("  500"), 500);
	EXPECT_EQ(ComputeCheatAmountArg("500abc"), 500);
	EXPECT_EQ(ComputeCheatAmountArg("+700"), 700);
}

TEST(CheatAmount, WireAmountNeverNegative)
{
	EXPECT_EQ(ComputeCheatAmountFromWire(0), 0);
	EXPECT_EQ(ComputeCheatAmountFromWire(300), 300);
	EXPECT_EQ(ComputeCheatAmountFromWire(INT_MAX), INT_MAX);
	EXPECT_EQ(ComputeCheatAmountFromWire(-1), 0);
	EXPECT_EQ(ComputeCheatAmountFromWire(INT_MIN), 0);
}

TEST(CheatAmount, OldDemosReadTwoBytesEverythingElseFour)
{
	EXPECT_EQ(ComputeCheatAmountWireBytes(true, kLongAmountDemoVersion - 1), 2);
	EXPECT_EQ(ComputeCheatAmountWireBytes(true, 0x215), 2);
	EXPECT_EQ(ComputeCheatAmountWireBytes(true, kLongAmountDemoVersion), 4);
	EXPECT_EQ(ComputeCheatAmountWireBytes(true, kLongAmountDemoVersion + 1), 4);
	// [Doruk-Aksoy] Live play and recording ignore a stale demover from an earlier playback.
	EXPECT_EQ(ComputeCheatAmountWireBytes(false, 0x215), 4);
	EXPECT_EQ(ComputeCheatAmountWireBytes(false, 0), 4);
}

TEST(CheatAmount, SaturatingAdd)
{
	EXPECT_EQ(ComputeSaturatingAdd(100, 400), 500);
	EXPECT_EQ(ComputeSaturatingAdd(100, INT_MAX), INT_MAX);
	EXPECT_EQ(ComputeSaturatingAdd(INT_MAX, 1), INT_MAX);
	EXPECT_EQ(ComputeSaturatingAdd(-100, INT_MIN), INT_MIN);
	EXPECT_EQ(ComputeSaturatingAdd(-5, 3), -2);
}

TEST(CheatAmount, SaturatingMul)
{
	EXPECT_EQ(ComputeSaturatingMul(200, 1000), 200000);
	EXPECT_EQ(ComputeSaturatingMul(200, INT_MAX), INT_MAX);
	EXPECT_EQ(ComputeSaturatingMul(-200, INT_MAX), INT_MIN);
	EXPECT_EQ(ComputeSaturatingMul(0, INT_MAX), 0);
	EXPECT_EQ(ComputeSaturatingMul(-3, -4), 12);
}
