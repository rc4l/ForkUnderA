// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "pieceorder_compute.h"

#include <algorithm>
#include <cstring>

namespace zx { namespace hwrender {

uint64_t ComputePieceOrderKey(bool blended, uintptr_t material, float distSq)
{
	const uint64_t blendBit = 1ull << 63;
	if (!blended) return (uint64_t)material & ~blendBit;
	// [rc4l] A non-negative float's bits order as it does, so the far-first order is their complement.
	uint32_t bits;
	std::memcpy(&bits, &distSq, sizeof(bits));
	return blendBit | (uint64_t)(0xFFFFFFFFu - bits);
}

void ComputeSortPieceOrder(std::vector<PieceOrderKey> &keys)
{
	std::sort(keys.begin(), keys.end(), [](const PieceOrderKey &a, const PieceOrderKey &b)
	{
		return a.key != b.key ? a.key < b.key : a.index < b.index;
	});
}

}} // namespace zx::hwrender
