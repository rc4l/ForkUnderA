// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The mesh pieces fua_look's ray passes through, nearest first, engine-free.

#ifndef ZX_LOOKHITS_COMPUTE_H
#define ZX_LOOKHITS_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] Files a piece's hit at distance t into the count nearest kept in ts and ids (at most cap), a piece once at its nearest; returns the new count.
int ComputeKeepNearestHit(float *ts, int *ids, int count, int cap, float t, int id);

}} // namespace zx::levelmesh

#endif
