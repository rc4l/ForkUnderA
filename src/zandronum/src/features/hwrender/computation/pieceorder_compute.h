// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The dynamic pieces' draw order as one key each: opaque before blended, opaque grouped by material, blended back to front, the index breaking ties.

#ifndef ZX_PIECEORDER_COMPUTE_H
#define ZX_PIECEORDER_COMPUTE_H

#include <cstdint>
#include <vector>

namespace zx { namespace hwrender {

struct PieceOrderKey
{
	uint64_t key;   // [rc4l] ComputePieceOrderKey's
	int index;      // [rc4l] the piece, which breaks ties so the order is the same every frame
};

// [rc4l] A piece's place in the order: blended pieces after every opaque one, opaque by material, blended by distance squared, farthest first.
uint64_t ComputePieceOrderKey(bool blended, uintptr_t material, float distSq);

// [rc4l] Sorts keys by key, then index, which sorting the pieces themselves did by reading two of them a comparison.
void ComputeSortPieceOrder(std::vector<PieceOrderKey> &keys);

}} // namespace zx::hwrender

#endif // ZX_PIECEORDER_COMPUTE_H
