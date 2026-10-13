// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/cameraseen_compute.h"

#include <gtest/gtest.h>

using namespace zx::frame;

TEST(CamerasSeen, ACameraIsSeenWhenAnyWalkedSurfaceShowsIt)
{
	const std::vector<int> cameras = { 40, 7, 99 };
	std::vector<int> textures = { 12, 99, 3, 12, 40 };
	std::vector<unsigned char> seen(9, 1);
	ComputeCamerasSeen(cameras, textures, seen);
	ASSERT_EQ(3u, seen.size());
	EXPECT_EQ(1, seen[0]);
	EXPECT_EQ(0, seen[1]);
	EXPECT_EQ(1, seen[2]);
}

TEST(CamerasSeen, NothingWalkedSeesNoCamera)
{
	const std::vector<int> cameras = { 5 };
	std::vector<int> textures;
	std::vector<unsigned char> seen;
	ComputeCamerasSeen(cameras, textures, seen);
	ASSERT_EQ(1u, seen.size());
	EXPECT_EQ(0, seen[0]);
}
