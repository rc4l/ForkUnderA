// [rc4l] MBF21 field-of-view cone test, used by A_JumpIfTarget/TracerInSight (and the FOV arg of the
// seek pointers). Pure so the fragile part -- BAM angle arithmetic that wraps around 0, including the
// minang>maxang split -- is unit-testable off-engine. The engine computes the direction angle to the
// other actor via R_PointToAngle2 and the facing angle; this decides whether it falls in the cone.
// Matches DSDA-Doom p_sight.c P_CheckFov.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_MBF21_FOV_COMPUTE_H
#define ZX_MBF21_FOV_COMPUTE_H

#include <cstdint>

namespace zx { namespace mbf21 {

// True when the BAM angle `dirAngle` (direction from the viewer to the other actor) lies within a
// cone of total width `fov` centred on `facingAngle`. All values are BAM (uint32) and wrap modulo
// 2^32. Callers use this only when fov > 0; a fov of 0 in a codepointer means "any direction" and is
// handled before calling (with fov==0 this returns true only for an exact facing match).
bool ComputeInFov(uint32_t dirAngle, uint32_t facingAngle, uint32_t fov);

}} // namespace zx::mbf21

#endif // ZX_MBF21_FOV_COMPUTE_H
