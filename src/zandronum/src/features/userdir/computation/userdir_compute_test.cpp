// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/userdir/computation/userdir_compute.h"

#include <gtest/gtest.h>

using zx::userdir::ComputeFuaUserPath;

TEST(ComputeFuaUserPath, BesideTheNodeCacheByDefault)
{
	EXPECT_EQ("C:/Users/me/AppData/Local/ForkUnderA", ComputeFuaUserPath(nullptr, "C:/Users/me/AppData/Local/zdoom/cache", "ForkUnderA"));
	EXPECT_EQ("C:/Users/me/AppData/Local/ForkUnderA", ComputeFuaUserPath("", "C:\\Users\\me\\AppData\\Local\\zdoom\\cache", "ForkUnderA"));
	EXPECT_EQ("/home/me/.config/ForkUnderA", ComputeFuaUserPath(nullptr, "/home/me/.config/zdoom/cache", "ForkUnderA"));
}

// [Doruk-Aksoy] A test launch's own folder, so its Continue records and keys never reach the player's.
TEST(ComputeFuaUserPath, AGivenFolderWins)
{
	EXPECT_EQ("C:/Temp/fuactl-ab/profile", ComputeFuaUserPath("C:\\Temp\\fuactl-ab\\profile\\", "C:/Users/me/AppData/Local/zdoom/cache", "ForkUnderA"));
	EXPECT_EQ("/tmp/p", ComputeFuaUserPath("/tmp/p//", "/x/zdoom/cache", "ForkUnderA"));
	EXPECT_EQ("/", ComputeFuaUserPath("/", "/x/zdoom/cache", "ForkUnderA"));
}

// [Doruk-Aksoy] A cache path too short to trim keeps what it has rather than cutting to nothing.
TEST(ComputeFuaUserPath, AShortCachePathIsKept)
{
	EXPECT_EQ("cache/ForkUnderA", ComputeFuaUserPath(nullptr, "cache", "ForkUnderA"));
	EXPECT_EQ("/cache/ForkUnderA", ComputeFuaUserPath(nullptr, "/cache", "ForkUnderA"));
	EXPECT_EQ("/ForkUnderA", ComputeFuaUserPath(nullptr, nullptr, "ForkUnderA"));
	EXPECT_EQ("/a/", ComputeFuaUserPath(nullptr, "/a/b/c", nullptr));
}
