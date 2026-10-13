// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fua_debug_orbit's aim: a circle around where the player was looking, the turn a hand on a mouse makes, worked out per frame from the clock.

#ifndef ZX_DEBUGORBIT_COMPUTE_H
#define ZX_DEBUGORBIT_COMPUTE_H

namespace zx { namespace frame {

// [rc4l] The aim offset at `seconds` into the orbit, in degrees: `radius` round a circle every `period` seconds, yaw right and pitch up from the start.
void ComputeOrbitOffset(double seconds, double radius, double period, double &yaw, double &pitch);

// [rc4l] Whole mouse-turn units (1/65536 of a turn, G_AddViewAngle's) to apply now to reach `wantDeg` when `doneUnits` are applied already.
int ComputeOrbitUnits(double wantDeg, long long doneUnits);

}} // namespace zx::frame

#endif // ZX_DEBUGORBIT_COMPUTE_H
