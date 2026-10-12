// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's FFlatVertex as the mesh's MeshVertex: the same five floats, asserted here, so GL's walk hands its vertices over without a copy.

#ifndef ZX_GLVERTEX_H
#define ZX_GLVERTEX_H

#include <stddef.h>
#include "gl/data/gl_vertexbuffer.h"
#include "features/levelmesh/staticmesh.h"

static_assert(sizeof(FFlatVertex) == sizeof(zx::levelmesh::MeshVertex), "FFlatVertex and MeshVertex must be the same size");
static_assert(offsetof(FFlatVertex, x) == offsetof(zx::levelmesh::MeshVertex, x) &&
              offsetof(FFlatVertex, z) == offsetof(zx::levelmesh::MeshVertex, z) &&
              offsetof(FFlatVertex, y) == offsetof(zx::levelmesh::MeshVertex, y) &&
              offsetof(FFlatVertex, u) == offsetof(zx::levelmesh::MeshVertex, u) &&
              offsetof(FFlatVertex, v) == offsetof(zx::levelmesh::MeshVertex, v), "FFlatVertex and MeshVertex must share a layout");

namespace zx { namespace levelmesh {

inline MeshVertex *AsMeshVertices(FFlatVertex *v) { return reinterpret_cast<MeshVertex *>(v); }
inline const MeshVertex *AsMeshVertices(const FFlatVertex *v) { return reinterpret_cast<const MeshVertex *>(v); }
inline FFlatVertex *AsFlatVertices(MeshVertex *v) { return reinterpret_cast<FFlatVertex *>(v); }

}} // namespace zx::levelmesh

#endif // ZX_GLVERTEX_H
