// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The decals glued to walls, built from the map for the sidedefs a view saw, as GLWall::DoDrawDecals draws them on each plain wall piece.

#ifndef ZX_WALLDECALS_H
#define ZX_WALLDECALS_H

namespace zx { namespace levelmesh {

// [rc4l] The decals on `sides` (sidedef indices) into the dynamic stream of the view being walked, cut to each light band of each part they mark; a WalkHooks::decals.
void BuildWallDecals(const int *sides, int count);

}} // namespace zx::levelmesh

#endif // ZX_WALLDECALS_H
