// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The render copy's polyobjects: which subsectors hold one and a copy of each one's polyobject BSP, linked and built between tics, since a tic clears the links and frees their nodes as a polyobject moves.

#ifndef ZX_GAMECOPY_POLYOBJS_H
#define ZX_GAMECOPY_POLYOBJS_H

#include "r_defs.h"

#include <vector>

namespace zx { namespace gamecopy {

// [rc4l] The copied polyobject BSP of a subsector, NULL for one no polyobject is in.
const FMiniBSP *PolyBSPOf(int subsector);
// [rc4l] The subsectors a polyobject is in, each once, in the order the polyobjects' links name them.
const std::vector<int> &PolySubsectors();

// [rc4l] Links every polyobject, builds the BSP of each subsector one is in where it is stale, and copies them; part of the fill, after the polyobjects' vertices.
void FillPolyBSPs();
// [rc4l] Moves the copied BSPs' vertices along their polyobjects' lines to the lines' blended vertices.
void BlendPolyBSPs();
// [rc4l] Forgets the last level's copies.
void InitPolyBSPs();

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_POLYOBJS_H
