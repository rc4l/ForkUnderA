// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/dynlayout_compute.h"

#include <gtest/gtest.h>

using namespace zx::hwrender;

TEST(DynLayout, PiecesGoBackToBackInDrawOrderSkippingEmptyOnes)
{
	// [rc4l] Piece 1 has no vertices; the draw order puts piece 3 first.
	const int order[4] = { 3, 0, 1, 2 };
	const unsigned vertices[4] = { 6, 0, 9, 6 };
	std::vector<DynSlot> out;
	EXPECT_EQ(21u, ComputeDynLayout(order, 4, vertices, 10, out));
	ASSERT_EQ(3u, out.size());
	EXPECT_EQ(3, out[0].piece);
	EXPECT_EQ(0u, out[0].first);
	EXPECT_EQ(6u, out[0].count);
	EXPECT_EQ(0, out[0].record);
	EXPECT_EQ(0, out[1].piece);
	EXPECT_EQ(6u, out[1].first);
	EXPECT_EQ(1, out[1].record);
	EXPECT_EQ(2, out[2].piece);
	EXPECT_EQ(12u, out[2].first);
	EXPECT_EQ(9u, out[2].count);
	EXPECT_EQ(2, out[2].record);
}

TEST(DynLayout, PiecesPastTheRecordsLeftShareNone)
{
	const int order[3] = { 0, 1, 2 };
	const unsigned vertices[3] = { 6, 6, 6 };
	std::vector<DynSlot> out(5);
	EXPECT_EQ(18u, ComputeDynLayout(order, 3, vertices, 1, out));
	ASSERT_EQ(3u, out.size());
	EXPECT_EQ(0, out[0].record);
	EXPECT_EQ(-1, out[1].record);
	EXPECT_EQ(-1, out[2].record);
	EXPECT_EQ(0u, ComputeDynLayout(order, 0, vertices, 1, out));
	EXPECT_TRUE(out.empty());
}
