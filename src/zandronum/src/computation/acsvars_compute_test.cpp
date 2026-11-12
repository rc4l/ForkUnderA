// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/acsvars_compute.h"

#include <gtest/gtest.h>

using zx::ComputeNewGameClearsAcsVars;

TEST(NewGameAcsVars, AGameOfItsOwnClearsThem)
{
	EXPECT_TRUE(ComputeNewGameClearsAcsVars(false, false));
	EXPECT_TRUE(ComputeNewGameClearsAcsVars(false, true));
}

TEST(NewGameAcsVars, AnActiveClientKeepsThemAcrossItsServersLevels)
{
	EXPECT_FALSE(ComputeNewGameClearsAcsVars(true, true));
}

TEST(NewGameAcsVars, AClientStillJoiningStartsClear)
{
	EXPECT_TRUE(ComputeNewGameClearsAcsVars(true, false));
}
