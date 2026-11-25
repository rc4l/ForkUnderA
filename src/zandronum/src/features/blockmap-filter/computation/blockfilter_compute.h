// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] The distance test PIT_CheckThing starts with, answered from a candidate's blockmap node instead of the actor.
#ifndef ZX_BLOCKFILTER_COMPUTE_H
#define ZX_BLOCKFILTER_COMPUTE_H

#include <cstdint>

// [Doruk-Aksoy] Whether a candidate at (candX, candY) is too far from a mover at (moverX, moverY) to touch it, measured with the candidate's attack radius against a missile and its radius otherwise, as PIT_CheckThing's first distance test.
bool ComputeBlockCandidateMissed(int64_t candX, int64_t candY, int64_t candRadius, int64_t candAttackRadius,
	int64_t moverX, int64_t moverY, int64_t moverRadius, bool moverIsMissile);

#endif
