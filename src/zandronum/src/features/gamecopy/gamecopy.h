// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The render copy of the map: filled from the live game between tics, blended between two tics once per view, and checked against the live game by fua_copy_check while tics run inline.

#ifndef ZX_GAMECOPY_H
#define ZX_GAMECOPY_H

#include "features/gamecopy/computation/blend_compute.h"

namespace zx { namespace gamecopy {

// [rc4l] Copies every sector, side, line, polyobject vertex and actor the renderer reads, bumping each one's change counters where its copy changed, and gathers the interpolations' blends; between tics only, and once a tic (ComputeFillDue).
void FillMap();
// [rc4l] FillMap whether or not a tic ran since, for a view asked for before the level's first fill.
void FillNow();
// [rc4l] A new level: every copy taken whole and its counters started over.
void InitLevel();
// [rc4l] Moves the copy's moving planes, scrolls and polyobjects to smoothratio of the way through the last tic, as the interpolations move the live game for one view.
void BlendMap(zx::Fixed smoothratio);
// [rc4l] Where each interpolation adds its values at the start and end of the tic while FillMap gathers them.
Blends &GatheringBlends();

// [rc4l] Whether fua_copy_check is on, which has a view GL does not draw move the live game too.
bool CopyCheckOn();

// [rc4l] fua_copy_check, before a frame's views: what no blend moves still matches the live game, which only something changing the game outside a tic breaks.
void CheckBeforeViews();
// [rc4l] fua_copy_check, after a view's interpolations moved the live game: the copy's blends landed where they did.
void CheckAfterInterpolation();

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_H
