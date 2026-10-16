// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where a sector plane's texture sits on it, as the mesh, the walls' 3D faces and the horizons all lay it.

#ifndef ZX_PLANEUV_H
#define ZX_PLANEUV_H

#include "features/surfaces/sectorplane.h"   // [rc4l] GLSectorPlane
#include "features/materials/materials.h"   // [rc4l] MaterialRef
#include "features/levelmesh/computation/flatmesh_compute.h"   // [rc4l] PlaneUVTransform

namespace zx { namespace surfaces {

// [rc4l] The plane's offsets, scale and turn with its material's size, as one transform.
void PlaneUVFor(const GLSectorPlane &plane, zx::materials::MaterialRef mat, zx::levelmesh::PlaneUVTransform &out);

}} // namespace zx::surfaces

#endif // ZX_PLANEUV_H
