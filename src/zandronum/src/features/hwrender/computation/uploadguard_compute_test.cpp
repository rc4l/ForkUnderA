// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "uploadguard_compute.h"

#include <gtest/gtest.h>

using namespace zx::hwrender;

// [rc4l] The light grid and the 2D layer were sent every frame, the same bytes while the view stood still.
TEST(UploadGuard, TheSameBytesToTheSameBufferAreNotSentAgain)
{
	UploadShadow s;
	const unsigned a[3] = { 1, 2, 3 };
	EXPECT_TRUE(ComputeUploadChanged(s, 7, a, sizeof(a)));
	EXPECT_FALSE(ComputeUploadChanged(s, 7, a, sizeof(a)));
	const unsigned b[3] = { 1, 2, 4 };
	EXPECT_TRUE(ComputeUploadChanged(s, 7, b, sizeof(b)));
	EXPECT_FALSE(ComputeUploadChanged(s, 7, b, sizeof(b)));
}

TEST(UploadGuard, ABufferMadeAgainOrAnotherSizeIsAlwaysSent)
{
	UploadShadow s;
	const unsigned a[3] = { 1, 2, 3 };
	EXPECT_TRUE(ComputeUploadChanged(s, 7, a, sizeof(a)));
	EXPECT_TRUE(ComputeUploadChanged(s, 8, a, sizeof(a)));
	EXPECT_TRUE(ComputeUploadChanged(s, 8, a, sizeof(unsigned) * 2));
	EXPECT_FALSE(ComputeUploadChanged(s, 8, a, sizeof(unsigned) * 2));
}

TEST(UploadGuard, NothingSentTwiceIsUnchanged)
{
	UploadShadow s;
	EXPECT_TRUE(ComputeUploadChanged(s, 3, nullptr, 0));
	EXPECT_FALSE(ComputeUploadChanged(s, 3, nullptr, 0));
}
