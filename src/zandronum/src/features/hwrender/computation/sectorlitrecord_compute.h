// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A level surface lit from the sector light table: its record carries where its light comes from instead of the shading, and the scene shader works the shading out each frame from the table (fuaSectorLit, dgshared.h).

#ifndef ZX_SECTORLITRECORD_COMPUTE_H
#define ZX_SECTORLITRECORD_COMPUTE_H

#include "features/hwrender/computation/spritepack_compute.h"   // [rc4l] ScenePieceData
#include "features/levelmesh/computation/sectorlight_compute.h"
#include "features/levelmesh/computation/shading_compute.h"

namespace zx { namespace hwrender {

// [rc4l] Writes r and noFog over pd's shading and marks it kTexFlagSectorLit: the sector, kind, side light and contrast in its colour, its absolute, kept-in-fog and no-fog flags in its fog colour.
void ComputeSectorLitRecord(ScenePieceData &pd, const zx::levelmesh::SectorLightRef &r, bool noFog);

// [rc4l] The shading the scene shader gives a record ComputeSectorLitRecord marked, from its sector's entry, the level's light settings and the player's extra light, as ShadePiece and FillPieceData would have baked it.
void ComputeSectorLitShade(ScenePieceData &pd, const zx::levelmesh::SectorLightEntry &e, zx::levelmesh::ShadingEnv env, int extraLight);

// [rc4l] The sectors to make the sector light table hold for a level of `sectors`: a quarter more, at least 1024, so a level loaded after a smaller one rarely makes it again with the pipelines that bind it.
unsigned ComputeSectorLightCapacity(int sectors);

}} // namespace zx::hwrender

#endif // ZX_SECTORLITRECORD_COMPUTE_H
