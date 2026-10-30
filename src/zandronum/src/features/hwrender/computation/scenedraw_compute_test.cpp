// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/hwrender/computation/scenedraw_compute.h"

#include <gtest/gtest.h>

using zx::hwrender::ComputeSceneDrawRefusal;

TEST(SceneDrawRefusal, InALevelWithTheSceneUpMayDraw)
{
	EXPECT_EQ(nullptr, ComputeSceneDrawRefusal(true, true));
}

TEST(SceneDrawRefusal, InALevelWithoutTheSceneAsksForIt)
{
	EXPECT_STREQ("call fua_diligent_scene first", ComputeSceneDrawRefusal(true, false));
}

// [Doruk-Aksoy] The scene the last level uploaded is still up after endgame, which is what let the shot draw the freed level.
TEST(SceneDrawRefusal, OutsideALevelWithTheLastLevelsSceneStillUpRefuses)
{
	EXPECT_STREQ("not in a level", ComputeSceneDrawRefusal(false, true));
}

TEST(SceneDrawRefusal, OutsideALevelWithNoSceneSaysItIsNotInALevel)
{
	EXPECT_STREQ("not in a level", ComputeSceneDrawRefusal(false, false));
}
