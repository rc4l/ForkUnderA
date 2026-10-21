// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A loaded level readied for either renderer (GL's gl_PreprocessLevel and gl_CleanLevelData, less what is GL's own).

#ifndef ZX_LEVELPREP_H
#define ZX_LEVELPREP_H

namespace zx { namespace levelprep {

// [rc4l] Map sections, each sector's subsectors and vertices, transparent doors, the vertices' sectors and height lists, side segs in order, the stacked portals and MAPINFO's gl_renderer options.
void PrepareLevel();

// [rc4l] Free what PrepareLevel made, and the level's light actors, before the level goes.
void CleanLevel();

}} // namespace zx::levelprep

#endif // ZX_LEVELPREP_H
