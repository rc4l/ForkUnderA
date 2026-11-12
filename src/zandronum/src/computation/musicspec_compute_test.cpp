// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/musicspec_compute.h"

#include <gtest/gtest.h>

using zx::ComputeMusicSpec;

TEST(MusicSpec, ANameAloneHasTrackZeroAndNoneGiven)
{
	const zx::MusicSpec spec = ComputeMusicSpec("D_RUNNIN");
	EXPECT_EQ(spec.name, "D_RUNNIN");
	EXPECT_EQ(spec.order, 0);
	EXPECT_FALSE(spec.hasOrder);
}

TEST(MusicSpec, AColonGivesTheTrackAndEndsTheName)
{
	const zx::MusicSpec spec = ComputeMusicSpec("music/theme.nsf:3");
	EXPECT_EQ(spec.name, "music/theme.nsf");
	EXPECT_EQ(spec.order, 3);
	EXPECT_TRUE(spec.hasOrder);
	EXPECT_EQ(ComputeMusicSpec("D_X:junk").order, 0);
}

TEST(MusicSpec, NoTextIsAnEmptyName)
{
	EXPECT_EQ(ComputeMusicSpec(nullptr).name, "");
	EXPECT_EQ(ComputeMusicSpec("").name, "");
}
