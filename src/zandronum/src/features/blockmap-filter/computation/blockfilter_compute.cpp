// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/blockmap-filter/computation/blockfilter_compute.h"

namespace
{
int64_t Abs(int64_t v) { return v < 0 ? -v : v; }
}

bool ComputeBlockCandidateMissed(int64_t candX, int64_t candY, int64_t candRadius, int64_t candAttackRadius,
	int64_t moverX, int64_t moverY, int64_t moverRadius, bool moverIsMissile)
{
	const int64_t blockdist = (moverIsMissile ? candAttackRadius : candRadius) + moverRadius;
	return Abs(candX - moverX) >= blockdist || Abs(candY - moverY) >= blockdist;
}
