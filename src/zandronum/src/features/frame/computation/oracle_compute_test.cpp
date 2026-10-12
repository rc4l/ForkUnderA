// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/oracle_compute.h"

#include <gtest/gtest.h>

using namespace zx::frame;

TEST(ComputeOracleDiffer, OnlyTheFirstFewAreDescribed)
{
	OracleTally t;
	EXPECT_TRUE(ComputeOracleDiffer(t, 2));
	EXPECT_TRUE(ComputeOracleDiffer(t, 2));
	EXPECT_FALSE(ComputeOracleDiffer(t, 2));
	EXPECT_EQ(3, t.differed);
}

TEST(ComputeOracleRoom, CountAndLengthBothCap)
{
	OracleTally t;
	EXPECT_TRUE(ComputeOracleRoom(t, 1, 10));
	t.firsts = "0123456789";
	EXPECT_FALSE(ComputeOracleRoom(t, 1, 10));
	t.firsts.clear();
	t.differed = 1;
	EXPECT_FALSE(ComputeOracleRoom(t, 1, 10));
}

TEST(ComputeOracleNote, AppendsFormattedLinesAndResetClearsAll)
{
	OracleTally t;
	t.checked = 4;
	ComputeOracleDiffer(t, 8);
	ComputeOracleNote(t, "  side %d: %s\n", 7, "GL only");
	ComputeOracleNote(t, "  side %d: %s\n", 9, "ours only");
	EXPECT_EQ("  side 7: GL only\n  side 9: ours only\n", t.firsts);
	ComputeOracleReset(t);
	EXPECT_EQ(0, t.checked);
	EXPECT_EQ(0, t.differed);
	EXPECT_TRUE(t.firsts.empty());
}
