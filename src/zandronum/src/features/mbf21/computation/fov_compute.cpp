// [rc4l] Implementation of the MBF21 FOV cone test. See fov_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fov_compute.h"

namespace zx { namespace mbf21 {

bool ComputeInFov(uint32_t dirAngle, uint32_t facingAngle, uint32_t fov)
{
	// Unsigned (BAM) arithmetic: these deliberately wrap modulo 2^32.
	const uint32_t minang = facingAngle - fov / 2;
	const uint32_t maxang = facingAngle + fov / 2;

	// When the cone straddles 0 the low edge is numerically above the high edge, so the in-cone test
	// flips from AND to OR.
	return (minang > maxang) ? (dirAngle >= minang || dirAngle <= maxang)
	                         : (dirAngle >= minang && dirAngle <= maxang);
}

}} // namespace zx::mbf21
