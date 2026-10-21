// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wipe/computation/wipe_compute.h"

#include <gtest/gtest.h>

using namespace zx::wipe;

TEST(WipeMelt, ColumnsStartLateByUpToFifteenTicsAndCreepFromTheirNeighbour)
{
	unsigned char r[kMeltColumns];
	for (int i = 0; i < kMeltColumns; i++) r[i] = (unsigned char)(i * 37);
	int y[kMeltColumns];
	ComputeMeltStart(r, y);
	EXPECT_EQ(-(r[0] & 15), y[0]);
	for (int i = 1; i < kMeltColumns; i++)
	{
		EXPECT_GE(y[i], -15) << i;
		EXPECT_LE(y[i], 0) << i;
		EXPECT_LE(y[i] - y[i - 1], 1) << i;
		EXPECT_GE(y[i] - y[i - 1], -1) << i;
	}
	// [rc4l] A byte of 0 steps a column one tic later than its neighbour, 2 one earlier, both held to the range.
	unsigned char low[kMeltColumns] = { 15, 0, 0 };
	ComputeMeltStart(low, y);
	EXPECT_EQ(-15, y[0]);
	EXPECT_EQ(-15, y[1]);
	unsigned char high[kMeltColumns] = { 0, 2, 2 };
	ComputeMeltStart(high, y);
	EXPECT_EQ(0, y[1]);
}

TEST(WipeMelt, AColumnWaitsThenFallsFasterUntilEveryColumnIsDown)
{
	int y[kMeltColumns];
	for (int i = 0; i < kMeltColumns; i++) y[i] = 0;
	y[0] = -2;
	EXPECT_FALSE(ComputeMeltTic(y));
	EXPECT_EQ(-1, y[0]);
	EXPECT_EQ(1, y[1]);
	EXPECT_FALSE(ComputeMeltTic(y));
	EXPECT_EQ(0, y[0]);
	EXPECT_EQ(3, y[1]);
	// [rc4l] Past 16 rows a column falls eight at a time, and stops at the bottom.
	y[1] = 20;
	y[2] = 196;
	ComputeMeltTic(y);
	EXPECT_EQ(28, y[1]);
	EXPECT_EQ(kMeltHeight, y[2]);
	for (int i = 0; i < kMeltColumns; i++) y[i] = kMeltHeight;
	EXPECT_TRUE(ComputeMeltTic(y));
}

TEST(WipeMelt, AStripShowsTheTopOfItsColumnShiftedDownByItsFall)
{
	MeltStrip s;
	ASSERT_TRUE(ComputeMeltStrip(1, 100, 640, 400, s));
	EXPECT_EQ(2.f, s.x);
	EXPECT_EQ(200.f, s.y);
	EXPECT_EQ(2.f, s.w);
	EXPECT_EQ(200.f, s.h);
	EXPECT_FLOAT_EQ(2.f / 640, s.u1);
	EXPECT_FLOAT_EQ(4.f / 640, s.u2);
	EXPECT_FLOAT_EQ(0.5f, s.v2);
	// [rc4l] A column still waiting has not moved, and one fallen to the bottom is gone.
	ASSERT_TRUE(ComputeMeltStrip(0, -5, 640, 400, s));
	EXPECT_EQ(0.f, s.y);
	EXPECT_FALSE(ComputeMeltStrip(0, kMeltHeight, 640, 400, s));
}

TEST(WipeCrossfade, TheNewScreenFadesInOverThirtyTwoTics)
{
	EXPECT_EQ(0.f, ComputeCrossfadeAlpha(-3));
	EXPECT_EQ(0.5f, ComputeCrossfadeAlpha(16));
	EXPECT_EQ(1.f, ComputeCrossfadeAlpha(40));
}

TEST(WipeBurn, TheMaskIsTheFireDoubledAndClamped)
{
	const unsigned char fire[3] = { 0, 100, 200 };
	unsigned char alpha[3];
	ComputeBurnMask(fire, 3, alpha);
	EXPECT_EQ(0, alpha[0]);
	EXPECT_EQ(200, alpha[1]);
	EXPECT_EQ(255, alpha[2]);
}
