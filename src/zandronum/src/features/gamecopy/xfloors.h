// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The render copy's 3D floors and light lists: a tic rebuilds them, deleting and creating 3D floors as they split, so the renderer reads copies whose planes, textures and light levels are the copy's own.

#ifndef ZX_GAMECOPY_XFLOORS_H
#define ZX_GAMECOPY_XFLOORS_H

#include "r_defs.h"

namespace zx { namespace gamecopy {

// [rc4l] Which sectors carry 3D floors or a light list, found once a level is set up.
void InitXFloors();
// [rc4l] Copies each such sector's 3D floors and light list into rcXFloor, bumping its rcShape where they changed; between tics, after the sectors' own copy.
void FillXFloors();
// [rc4l] Moves the copied 3D floors' planes and light bands to their models' blended planes.
void BlendXFloors();

// [rc4l] The colormap and blend a copied 3D floor's GetColormap and GetBlend gave at the fill.
FDynamicColormap *XFloorColormap(const F3DFloor *copy);
PalEntry XFloorBlend(const F3DFloor *copy);

// [rc4l] sector_t::GetHeightSec on the copy: the height transfer a sector draws with, none when the transfer is ignored or a copied 3D floor draws planes.
sector_t *HeightSecOf(const sector_t *sector);

// [rc4l] P_GetPlaneLight on a sector's copied light list.
lightlist_t *GetPlaneLight(sector_t *sector, const secplane_core_t &plane, bool underside);

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_XFLOORS_H
