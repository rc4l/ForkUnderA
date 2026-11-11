// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] fua_debug_aimassist: the local player's ticcmd turned onto the nearest monster in sight, for recorded fights whose shots must land.
#ifndef ZX_AIMASSIST_H
#define ZX_AIMASSIST_H

struct ticcmd_t;

namespace zx { namespace aimassist {

// [Doruk-Aksoy] Turns a single-player ticcmd onto a monster before it is recorded and run, called in the handoff while no tic runs.
void Apply(ticcmd_t *cmd, int playernum);

}} // namespace zx::aimassist

#endif
