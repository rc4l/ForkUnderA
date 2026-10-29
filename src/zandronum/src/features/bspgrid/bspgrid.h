// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] The level's two BSP trees, the game's and the renderer's, each with a grid saying where a point's walk to its subsector can start.
#ifndef ZX_BSPGRID_H
#define ZX_BSPGRID_H
#include "basictypes.h"
namespace zx { namespace bspgrid {
// [Doruk-Aksoy] Both trees' grids, once the level's nodes are final.
void BuildForLevel();
// [Doruk-Aksoy] The level's nodes are about to be freed, so no grid is read until the next level's are built.
void ForgetLevel();
// [Doruk-Aksoy] Where P_PointInSubsector's walk starts: an index into gamenodes, or -1 - an index into gamesubsectors.
int GameStart(fixed_t x, fixed_t y);
// [Doruk-Aksoy] Where R_PointInSubsector's walk starts: an index into nodes, or -1 - an index into subsectors.
int RenderStart(fixed_t x, fixed_t y);
}} // namespace zx::bspgrid
#endif // ZX_BSPGRID_H
