// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "lookhits_compute.h"

#include <gtest/gtest.h>

using namespace zx::levelmesh;

// [rc4l] WDI19's pool: the additive water was the only piece fua_look named, and the floor whose light it hid was the fault.
TEST(LookHitsCompute, KeepsTheNearestFewInOrder)
{
	float ts[3];
	int ids[3];
	int n = 0;
	n = ComputeKeepNearestHit(ts, ids, n, 3, 50.f, 7);
	n = ComputeKeepNearestHit(ts, ids, n, 3, 20.f, 4);
	n = ComputeKeepNearestHit(ts, ids, n, 3, 80.f, 9);
	n = ComputeKeepNearestHit(ts, ids, n, 3, 90.f, 2);  // [rc4l] farther than every kept one with no room left
	ASSERT_EQ(3, n);
	EXPECT_EQ(4, ids[0]);
	EXPECT_EQ(7, ids[1]);
	EXPECT_EQ(9, ids[2]);
	n = ComputeKeepNearestHit(ts, ids, n, 3, 30.f, 5);  // [rc4l] pushes the farthest out
	EXPECT_EQ(3, n);
	EXPECT_EQ(5, ids[1]);
	EXPECT_EQ(7, ids[2]);
	EXPECT_FLOAT_EQ(50.f, ts[2]);
}

TEST(LookHitsCompute, APieceIsListedOnceAtItsNearestTriangle)
{
	float ts[3];
	int ids[3];
	int n = 0;
	n = ComputeKeepNearestHit(ts, ids, n, 3, 50.f, 7);
	n = ComputeKeepNearestHit(ts, ids, n, 3, 60.f, 8);
	n = ComputeKeepNearestHit(ts, ids, n, 3, 70.f, 7);  // [rc4l] a farther triangle of a kept piece
	ASSERT_EQ(2, n);
	EXPECT_FLOAT_EQ(50.f, ts[0]);
	n = ComputeKeepNearestHit(ts, ids, n, 3, 55.f, 8);  // [rc4l] a nearer one moves it
	ASSERT_EQ(2, n);
	EXPECT_EQ(7, ids[0]);
	EXPECT_EQ(8, ids[1]);
	EXPECT_FLOAT_EQ(55.f, ts[1]);
	n = ComputeKeepNearestHit(ts, ids, n, 3, 10.f, 8);  // [rc4l] and past another
	ASSERT_EQ(2, n);
	EXPECT_EQ(8, ids[0]);
	EXPECT_EQ(7, ids[1]);
	n = ComputeKeepNearestHit(ts, ids, n, 3, 5.f, 8);   // [rc4l] the nearest moving nearer still, with a piece behind it to close up over
	ASSERT_EQ(2, n);
	EXPECT_EQ(8, ids[0]);
	EXPECT_FLOAT_EQ(5.f, ts[0]);
	EXPECT_EQ(7, ids[1]);
	EXPECT_FLOAT_EQ(50.f, ts[1]);
}
