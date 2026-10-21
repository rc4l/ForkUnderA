// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/stereo_compute.h"

using namespace zx::hwrender;

TEST(Stereo, MonoIsOneMiddleEyeSeeingEverything)
{
	for (int mode : { 0, 3, 4, 7, -1 })
	{
		const StereoMode s = ComputeStereoMode(mode);
		EXPECT_EQ(1, s.eyes);
		EXPECT_EQ(0.f, s.shift[0]);
		EXPECT_EQ(1.f, s.mask[0][0]); EXPECT_EQ(1.f, s.mask[0][2]);
	}
}

// [rc4l] Red/cyan gives the left eye red and the right green and blue; green/magenta the left green and the right red and blue.
TEST(Stereo, AnAnaglyphSplitsTheChannelsBetweenTwoEyes)
{
	const StereoMode rc = ComputeStereoMode(2);
	ASSERT_EQ(2, rc.eyes);
	EXPECT_EQ(-0.5f, rc.shift[0]); EXPECT_EQ(0.5f, rc.shift[1]);
	EXPECT_EQ(1.f, rc.mask[0][0]); EXPECT_EQ(0.f, rc.mask[0][1]); EXPECT_EQ(0.f, rc.mask[0][2]);
	EXPECT_EQ(0.f, rc.mask[1][0]); EXPECT_EQ(1.f, rc.mask[1][1]); EXPECT_EQ(1.f, rc.mask[1][2]);
	const StereoMode gm = ComputeStereoMode(1);
	EXPECT_EQ(0.f, gm.mask[0][0]); EXPECT_EQ(1.f, gm.mask[0][1]);
	EXPECT_EQ(1.f, gm.mask[1][0]); EXPECT_EQ(0.f, gm.mask[1][1]); EXPECT_EQ(1.f, gm.mask[1][2]);
}

TEST(Stereo, OneEyeAloneIsShiftedToItsSide)
{
	const StereoMode l = ComputeStereoMode(5), r = ComputeStereoMode(6);
	EXPECT_EQ(1, l.eyes); EXPECT_EQ(-0.5f, l.shift[0]);
	EXPECT_EQ(1, r.eyes); EXPECT_EQ(0.5f, r.shift[0]);
}

// [rc4l] At yaw 0 a shift moves the eye along x the other way, at 90 along y, scaled to map units.
TEST(Stereo, TheEyeMovesAcrossTheView)
{
	float dx, dy;
	ComputeEyeOffset(0.031f, 0.f, 41.f, dx, dy);
	EXPECT_NEAR(dx, -1.271f, 1e-4f);
	EXPECT_NEAR(dy, 0.f, 1e-5f);
	ComputeEyeOffset(0.031f, 90.f, 41.f, dx, dy);
	EXPECT_NEAR(dx, 0.f, 1e-4f);
	EXPECT_NEAR(dy, 1.271f, 1e-4f);
}

// [rc4l] A 90 degree view at aspect 1 has a half width of 1, so the shift is the eye's over the screen distance; a degenerate view leans nowhere.
TEST(Stereo, TheFrustumLeansByTheShiftOverTheScreenDistance)
{
	EXPECT_NEAR(ComputeEyeNdcShift(0.031f, 0.8f, 3.14159265f * 0.5f, 1.f), 0.03875f, 1e-5f);
	EXPECT_EQ(0.f, ComputeEyeNdcShift(0.031f, 0.f, 1.f, 1.f));
	EXPECT_EQ(0.f, ComputeEyeNdcShift(0.031f, 0.8f, 0.f, 1.f));
	float m[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, -1,  0, 0, 0, 2 };
	ComputeShiftNdcX(0.5f, m);
	EXPECT_EQ(1.f, m[0]);
	EXPECT_EQ(-0.5f, m[8]);   // [rc4l] the column whose w is -1 moves x by -0.5
	EXPECT_EQ(1.f, m[12]);    // [rc4l] the translation, w 2, by 1
}
