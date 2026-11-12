// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/checkscript_compute.h"

#include <gtest/gtest.h>

using zx::ComputeCheckScriptByName;
using zx::ComputeCheckScriptNumber;

TEST(CheckScript, ByNameOnlyWhenTheSecondArgumentSaysSo)
{
	EXPECT_TRUE(ComputeCheckScriptByName(2, 1));
	EXPECT_FALSE(ComputeCheckScriptByName(2, 0));
	EXPECT_FALSE(ComputeCheckScriptByName(1, 1));
}

TEST(CheckScript, ANamedScriptsNumberIsItsNamesNegative)
{
	EXPECT_EQ(ComputeCheckScriptNumber(true, 5, 1234), -1234);
	EXPECT_EQ(ComputeCheckScriptNumber(false, 5, 1234), 5);
}
