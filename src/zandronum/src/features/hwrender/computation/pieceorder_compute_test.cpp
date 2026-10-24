// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "pieceorder_compute.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

using namespace zx::hwrender;

namespace {

struct Piece { bool blended; uintptr_t material; float dist; };

// [rc4l] The comparator BuildDynamic sorted the pieces with before the keys.
bool Before(const std::vector<Piece> &p, int ia, int ib)
{
	const Piece &a = p[ia], &b = p[ib];
	if (a.blended != b.blended) return !a.blended;
	if (!a.blended)
	{
		if (a.material != b.material) return a.material < b.material;
		return ia < ib;
	}
	if (a.dist != b.dist) return a.dist > b.dist;
	return ia < ib;
}

} // namespace

// [rc4l] Sorting the pieces read two MeshPieces a comparison, a twentieth of a Complex Doom fight's frame.
TEST(PieceOrder, TheKeysOrderPiecesAsTheComparatorDid)
{
	std::mt19937 rng(7);
	std::uniform_int_distribution<int> coin(0, 1), mat(1, 6), dist(0, 40);
	std::vector<Piece> pieces;
	for (int i = 0; i < 500; i++) pieces.push_back({ coin(rng) == 1, (uintptr_t)(0x10000 + 64 * mat(rng)), dist(rng) * 0.5f });
	std::vector<int> want;
	for (int i = 0; i < (int)pieces.size(); i++) want.push_back(i);
	std::sort(want.begin(), want.end(), [&](int a, int b) { return Before(pieces, a, b); });
	std::vector<PieceOrderKey> keys;
	for (int i = 0; i < (int)pieces.size(); i++) keys.push_back({ ComputePieceOrderKey(pieces[i].blended, pieces[i].material, pieces[i].dist), i });
	ComputeSortPieceOrder(keys);
	for (size_t i = 0; i < keys.size(); i++) EXPECT_EQ(want[i], keys[i].index) << "at " << i;
}

TEST(PieceOrder, OpaqueComeFirstAndBlendedFarthestFirst)
{
	EXPECT_LT(ComputePieceOrderKey(false, (uintptr_t)-1, 0.f), ComputePieceOrderKey(true, 0, 1e30f));
	EXPECT_LT(ComputePieceOrderKey(true, 0, 100.f), ComputePieceOrderKey(true, 0, 1.f));
	EXPECT_LT(ComputePieceOrderKey(true, 0, 1.f), ComputePieceOrderKey(true, 0, 0.f));
	EXPECT_LT(ComputePieceOrderKey(false, 8, 0.f), ComputePieceOrderKey(false, 16, 0.f));
}
