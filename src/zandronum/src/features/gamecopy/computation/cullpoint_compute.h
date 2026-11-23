// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] An actor's frustum test inputs, packed beside its record so a frame's sweep reads 16 bytes an actor instead of the record's scattered fields.

#ifndef ZX_GAMECOPY_CULLPOINT_COMPUTE_H
#define ZX_GAMECOPY_CULLPOINT_COMPUTE_H

namespace zx { namespace gamecopy {

// [Doruk-Aksoy] Mesh-space centre (x, up, y) and padding of an actor's test, pad below zero for an actor in no sector.
struct ActorCullPoint { float x, up, y, pad; };

// [Doruk-Aksoy] The point the sweep tests: the actor's mid height, padded by its larger of radius and model reach, its height and 64.
ActorCullPoint ComputeActorCullPoint(bool inSector, float x, float y, float z, float height, float radius, float modelReach);

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_CULLPOINT_COMPUTE_H
