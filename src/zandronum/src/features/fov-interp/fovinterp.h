// [rc4l] fov-interp glue: the engine-facing entry point for the interpolated FOV.
//
// The arithmetic is in computation/fovinterp_compute.{h,cpp} (pure, tested). This layer is the
// only place that touches engine state — the CVAR, r_TicFrac, the pause/menu/demo predicates and
// the camera player — so the renderer's call site stays one line.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_FOVINTERP_H
#define ZX_FOVINTERP_H

struct player_t;

// The FOV to render this frame for `player`, in degrees, already clamped to R_SetFOV's range.
// Equals the player's current FOV plus the fraction of the next simulation step that the current
// sub-tic position calls for, and freezes at the simulated value while paused or in a menu.
float FOV_InterpolatedForFrame(player_t *player);

#endif // ZX_FOVINTERP_H
