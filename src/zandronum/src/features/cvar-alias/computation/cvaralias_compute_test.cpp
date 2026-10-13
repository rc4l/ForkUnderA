// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/cvar-alias/computation/cvaralias_compute.h"

#include <gtest/gtest.h>
#include <string.h>

using namespace zx::cvaralias;

TEST(CVarAlias, TheFuaNameStandsForVidFpsWhateverItsCase)
{
	EXPECT_STREQ("vid_fps", ComputeCVarAliasTarget("fua_vid_fps", strlen("fua_vid_fps")));
	EXPECT_STREQ("vid_fps", ComputeCVarAliasTarget("FUA_Vid_FPS", strlen("FUA_Vid_FPS")));
}

// [rc4l] The console looks a command's name up inside the whole line, so only its first `len` characters count.
TEST(CVarAlias, OnlyTheNamesLengthOfTheLineIsCompared)
{
	EXPECT_STREQ("vid_fps", ComputeCVarAliasTarget("fua_vid_fps 1", 11));
	EXPECT_EQ(NULL, ComputeCVarAliasTarget("fua_vid_fps 1", 10));
	EXPECT_EQ(NULL, ComputeCVarAliasTarget("fua_vid_fpsx", 12));
}

TEST(CVarAlias, ANameThatIsNoAliasStandsForNothing)
{
	EXPECT_EQ(NULL, ComputeCVarAliasTarget("vid_fps", 7));
	EXPECT_EQ(NULL, ComputeCVarAliasTarget("", 0));
	EXPECT_EQ(NULL, ComputeCVarAliasTarget(NULL, 0));
}
