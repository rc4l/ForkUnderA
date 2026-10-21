// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "menu/menuplatform_compute.h"

#include <gtest/gtest.h>

namespace {

using zx::MenuPlatform;

TEST(MenuPlatform, ThePlatformKeywordsInAnyCase)
{
	for (const char *k : { "Windows", "windows", "Mac", "MAC", "unix", "Unix", "Web", "web" })
		EXPECT_TRUE(zx::ComputeIsMenuPlatformKeyword(k)) << k;
	for (const char *k : { "ReadThis", "OpenAL", "", "Win", "Webs" })
		EXPECT_FALSE(zx::ComputeIsMenuPlatformKeyword(k)) << k;
	EXPECT_FALSE(zx::ComputeIsMenuPlatformKeyword(nullptr));
}

TEST(MenuPlatform, EachKeywordMatchesItsOwnPlatformOnly)
{
	EXPECT_TRUE(zx::ComputeMenuPlatformMatches("Windows", MenuPlatform::Windows));
	EXPECT_TRUE(zx::ComputeMenuPlatformMatches("Mac", MenuPlatform::Mac));
	EXPECT_TRUE(zx::ComputeMenuPlatformMatches("unix", MenuPlatform::Unix));
	EXPECT_TRUE(zx::ComputeMenuPlatformMatches("Web", MenuPlatform::Web));
	EXPECT_FALSE(zx::ComputeMenuPlatformMatches("Windows", MenuPlatform::Mac));
	EXPECT_FALSE(zx::ComputeMenuPlatformMatches("Mac", MenuPlatform::Unix));
}

// [rc4l] Emscripten defines __unix__, which offered a browser the desktop's renderers and window size.
TEST(MenuPlatform, ABrowserIsNotUnix)
{
	EXPECT_FALSE(zx::ComputeMenuPlatformMatches("unix", MenuPlatform::Web));
	EXPECT_FALSE(zx::ComputeMenuPlatformMatches("Web", MenuPlatform::Unix));
}

TEST(MenuPlatform, AnythingElseMatchesNoPlatform)
{
	EXPECT_FALSE(zx::ComputeMenuPlatformMatches("OpenAL", MenuPlatform::Unix));
	EXPECT_FALSE(zx::ComputeMenuPlatformMatches(nullptr, MenuPlatform::Web));
}

} // namespace
