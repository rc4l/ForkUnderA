// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "menu/callvotemenu_compute.h"

#include <gtest/gtest.h>

using zx::ComputeMissingVoteMenus;

TEST(MissingVoteMenus, AFreshMenuAddsEveryNamedSubmenu)
{
	EXPECT_EQ(ComputeMissingVoteMenus({ "ZA_CallVoteKick", "None" }, { "ZXVoteMenu", "", "ZXOther" }), (std::vector<unsigned>{ 0, 2 }));
}

TEST(MissingVoteMenus, AMenuOpenedAgainAddsNone)
{
	// [rc4l] A submenu closed with every menu at once left its parent's items behind, which theirs added a second time.
	EXPECT_TRUE(ComputeMissingVoteMenus({ "ZA_CallVoteKick", "zxvotemenu", "ZXOther" }, { "ZXVoteMenu", "ZXOther" }).empty());
}

TEST(MissingVoteMenus, OnlyTheOnesNotThereAreAdded)
{
	EXPECT_EQ(ComputeMissingVoteMenus({ "ZXVoteMenu" }, { "ZXVoteMenu", "ZXVoteMenus" }), (std::vector<unsigned>{ 1 }));
	EXPECT_TRUE(ComputeMissingVoteMenus({}, {}).empty());
}
