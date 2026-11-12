// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A map thing's angle in degrees as an actor's binary angle, the one conversion P_SpawnMapThing and GAME_ResetMap share.

#ifndef ZX_MAPANGLE_COMPUTE_H
#define ZX_MAPANGLE_COMPUTE_H

#include <cstdint>

namespace zx { namespace map {

// [rc4l] Ported from zandronum@28e63eb130d75a770e24a34373aab26973b76e37: every degree counts, where a map reset rounded down to a multiple of 45.
uint32_t ComputeMapThingAngle(int degrees);

}} // namespace zx::map

#endif // ZX_MAPANGLE_COMPUTE_H
