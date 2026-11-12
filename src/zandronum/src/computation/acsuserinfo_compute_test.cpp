// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/acsuserinfo_compute.h"

#include <gtest/gtest.h>

using zx::ComputeSendsUserinfoCvar;
using zx::ComputeUserinfoMayKick;

TEST(AcsUserinfo, AnUnsyncedCvarNeverGoesOut)
{
	EXPECT_FALSE(ComputeSendsUserinfoCvar(true, false, false));
	EXPECT_FALSE(ComputeSendsUserinfoCvar(true, true, true));
}

TEST(AcsUserinfo, AScriptSendsOnlyAModsCvar)
{
	EXPECT_TRUE(ComputeSendsUserinfoCvar(false, true, true));
	EXPECT_FALSE(ComputeSendsUserinfoCvar(false, false, true));
}

TEST(AcsUserinfo, TheMenuSendsAnySyncedCvar)
{
	EXPECT_TRUE(ComputeSendsUserinfoCvar(false, false, false));
	EXPECT_TRUE(ComputeSendsUserinfoCvar(false, true, false));
}

TEST(AcsUserinfo, OnlyAPlayersOwnChangesAfterTheFullUpdateMayKick)
{
	EXPECT_TRUE(ComputeUserinfoMayKick(false, false));
	EXPECT_FALSE(ComputeUserinfoMayKick(true, false));
	EXPECT_FALSE(ComputeUserinfoMayKick(false, true));
	EXPECT_FALSE(ComputeUserinfoMayKick(true, true));
}
