// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/linemirror_compute.h"

using namespace zx::hwrender;

// [rc4l] A mirror along y = 512 facing south (normal (0, -1), d -512): an eye 212 in front, facing it, sees from 212 behind it, facing back.
TEST(LineMirror, TheEyeIsReflectedAcrossThePlaneAndTurnedAround)
{
	float rx, ry;
	unsigned a;
	ASSERT_TRUE(ComputeMirrorEye(0.f, -1.f, -512.f, 192.f, 300.f, 0x40000000u, rx, ry, a));
	EXPECT_FLOAT_EQ(rx, 192.f);
	EXPECT_FLOAT_EQ(ry, 724.f);
	EXPECT_NEAR((double)a, (double)0xC0000000u, 2e5);   // [rc4l] north becomes south
	// [rc4l] Facing east along the mirror stays east.
	ASSERT_TRUE(ComputeMirrorEye(0.f, -1.f, -512.f, 192.f, 300.f, 0u, rx, ry, a));
	EXPECT_NEAR((double)a, 0.0, 2e5);
}

// [rc4l] An angle that comes out negative is wrapped into the circle, and an eye on or behind the plane sees nothing.
TEST(LineMirror, ANegativeTurnWrapsAndAnEyeBehindSeesNothing)
{
	float rx, ry;
	unsigned a;
	// [rc4l] A mirror facing east along x = 0: facing south-east (315 degrees) turns to -135, wrapped to south-west (225).
	ASSERT_TRUE(ComputeMirrorEye(1.f, 0.f, 0.f, 100.f, 0.f, 0xE0000000u, rx, ry, a));
	EXPECT_FLOAT_EQ(rx, -100.f);
	EXPECT_NEAR((double)a, (double)0xA0000000u, 2e5);
	EXPECT_FALSE(ComputeMirrorEye(1.f, 0.f, 0.f, -5.f, 0.f, 0u, rx, ry, a));
	EXPECT_FALSE(ComputeMirrorEye(1.f, 0.f, 0.f, 0.f, 0.f, 0u, rx, ry, a));
}

// [rc4l] GL draws mirrors to r_mirror_recursions deep and clears the next; with 0 even the first is cleared.
TEST(LineMirror, MirrorsAreDrawnToTheirRecursionDepth)
{
	EXPECT_TRUE(ComputeMirrorLevelDrawn(1, 4));
	EXPECT_TRUE(ComputeMirrorLevelDrawn(4, 4));
	EXPECT_FALSE(ComputeMirrorLevelDrawn(5, 4));
	EXPECT_FALSE(ComputeMirrorLevelDrawn(1, 0));
}
