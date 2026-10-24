// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/web/computation/offer_compute.h"

using zx::web::ComputeOfferedPath;

// [rc4l] A player's browser save lived only in Firefox's storage, and reaching it took the page's developer console.
TEST(ComputeOfferedPath, ASaveByNameOrAnyFileByPath)
{
	const std::string saves = "/home/web_user/.config/forkundera/";
	EXPECT_EQ(saves + "heavy.zds", ComputeOfferedPath("heavy", saves));
	EXPECT_EQ(saves + "heavy.zds", ComputeOfferedPath("heavy.zds", saves));
	EXPECT_EQ(saves + "notes.txt", ComputeOfferedPath("notes.txt", saves));
	EXPECT_EQ("/home/web_user/fight.zds", ComputeOfferedPath("/home/web_user/fight.zds", saves));
	EXPECT_EQ("/home/web_user/.config/forkundera/heavy.zds", ComputeOfferedPath("heavy", "/home/web_user/.config/forkundera"));
	EXPECT_EQ("heavy.zds", ComputeOfferedPath("heavy", ""));
	EXPECT_EQ("", ComputeOfferedPath("", saves));
}
