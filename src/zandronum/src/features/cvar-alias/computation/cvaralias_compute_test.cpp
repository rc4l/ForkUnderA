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

// [rc4l] Old configs, binds, menus and mods name the shared render settings by GL's gl_ names, which find their r_ names.
TEST(CVarAlias, GlsSharedSettingsFindTheirRendererNames)
{
	EXPECT_STREQ("r_lights", ComputeCVarAliasTarget("gl_lights", strlen("gl_lights")));
	EXPECT_STREQ("r_lights_size", ComputeCVarAliasTarget("GL_Lights_Size", strlen("GL_Lights_Size")));
	EXPECT_STREQ("r_texture_filter_anisotropic", ComputeCVarAliasTarget("gl_texture_filter_anisotropic", strlen("gl_texture_filter_anisotropic")));
	EXPECT_STREQ("r_seamless", ComputeCVarAliasTarget("gl_seamless", strlen("gl_seamless")));
	// [rc4l] GL's own settings keep their names.
	EXPECT_EQ(NULL, ComputeCVarAliasTarget("gl_wallmesh", strlen("gl_wallmesh")));
	EXPECT_EQ(NULL, ComputeCVarAliasTarget("gl_texture_format", strlen("gl_texture_format")));
}
