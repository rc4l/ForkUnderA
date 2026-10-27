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
	const bool packed[4] = { false, false, false, false };
	std::vector<DynSlot> out;
	const DynLayoutTotals t = ComputeDynLayout(order, 4, vertices, packed, 10, out);
	EXPECT_EQ(21u, t.stream);
	EXPECT_EQ(21u, t.general);
	EXPECT_EQ(0, t.sprites);
	ASSERT_EQ(3u, out.size());
	EXPECT_EQ(3, out[0].piece);
	EXPECT_EQ(0u, out[0].first);
	EXPECT_EQ(6u, out[0].count);
	EXPECT_EQ(0, out[0].record);
	EXPECT_EQ(-1, out[0].sprite);
	EXPECT_EQ(0, out[1].piece);
	EXPECT_EQ(6u, out[1].first);
	EXPECT_EQ(6u, out[1].general);
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
	const bool packed[3] = { false, false, false };
	std::vector<DynSlot> out(5);
	EXPECT_EQ(18u, ComputeDynLayout(order, 3, vertices, packed, 1, out).stream);
	ASSERT_EQ(3u, out.size());
	EXPECT_EQ(0, out[0].record);
	EXPECT_EQ(-1, out[1].record);
	EXPECT_EQ(-1, out[2].record);
	EXPECT_EQ(0u, ComputeDynLayout(order, 0, vertices, packed, 1, out).stream);
	EXPECT_TRUE(out.empty());
}

// [rc4l] A packed sprite takes its corners from its own words, so it needs no record and no general vertex, and the general pieces around it pack together.
TEST(DynLayout, PackedSpritesTakeASpriteInsteadOfARecord)
{
	const int order[4] = { 0, 1, 2, 3 };
	const unsigned vertices[4] = { 6, 6, 9, 6 };
	const bool packed[4] = { true, false, true, true };
	std::vector<DynSlot> out;
	const DynLayoutTotals t = ComputeDynLayout(order, 4, vertices, packed, 10, out);
	EXPECT_EQ(27u, t.stream);
	EXPECT_EQ(6u, t.general);
	EXPECT_EQ(3, t.sprites);
	ASSERT_EQ(4u, out.size());
	EXPECT_EQ(0, out[0].sprite);
	EXPECT_EQ(-1, out[0].record);
	EXPECT_EQ(-1, out[1].sprite);
	EXPECT_EQ(0u, out[1].general);
	EXPECT_EQ(0, out[1].record);
	EXPECT_EQ(1, out[2].sprite);
	EXPECT_EQ(12u, out[2].first);
	EXPECT_EQ(2, out[3].sprite);
	EXPECT_EQ(21u, out[3].first);
}

TEST(DynLayout, TheWordsBoundCoversEveryPiecePackedAndEveryVertexGeneral)
{
	// [rc4l] Two quads of four corners each: 56 words packed, or twelve general vertices of six words, plus four of alignment.
	EXPECT_EQ(2u * 28 + 12u * 6 + 4, ComputeDynWordsBound(2, 8, 0));
	EXPECT_EQ(4u, ComputeDynWordsBound(0, 0, 0));
	// [rc4l] An odd count rounds its half up.
	EXPECT_EQ(2u * 6 + 4, ComputeDynWordsBound(0, 1, 0));
	// [Doruk-Aksoy] Each model matrix takes sixteen more.
	EXPECT_EQ(4u + 2u * 16, ComputeDynWordsBound(0, 0, 2));
}

// [Doruk-Aksoy] A record names its model matrix by the word it starts at plus one, 0 for none.
TEST(DynLayout, AModelMatrixIsNamedByItsWordPlusOne)
{
	EXPECT_EQ(0.f, ComputeModelMatrixWord(400, -1));
	EXPECT_EQ(401.f, ComputeModelMatrixWord(400, 0));
	EXPECT_EQ(400.f + 2 * 16 + 1, ComputeModelMatrixWord(400, 2));
}

TEST(DynLayout, TheWordsCapacityLeavesHalfSpareAndAFloor)
{
	EXPECT_EQ(1u << 20, ComputeDynWordsCapacity(0));
	// [rc4l] The Complex Doom chamber fight's bound, 8,267 pieces over 33,068 vertices, fits the floor.
	EXPECT_EQ(1u << 20, ComputeDynWordsCapacity(ComputeDynWordsBound(8267, 33068, 0)));
	EXPECT_EQ(3000000u, ComputeDynWordsCapacity(2000000));
}
