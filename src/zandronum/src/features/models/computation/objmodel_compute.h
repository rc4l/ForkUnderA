// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_OBJMODEL_COMPUTE_H
#define ZX_OBJMODEL_COMPUTE_H

#include <cstddef>
#include <string>
#include <vector>

namespace zx { namespace models {

// [Doruk-Aksoy] One corner of an OBJ face: its position and its texture coordinate, 0-based, -1 for a coordinate it names none of.
struct ObjCorner
{
	int vertex;
	int uv;
};

// [Doruk-Aksoy] One `usemtl` run of faces: the material it names, empty before any, and its faces among ObjData's.
struct ObjSurface
{
	std::string material;
	unsigned firstFace, faceCount;
};

// [Doruk-Aksoy] An OBJ file as a model reads it: positions (x, y, z) and texture coordinates (u, v) as the file has them, each face's corners back to back, and the surfaces.
struct ObjData
{
	std::vector<float> positions;
	std::vector<float> uvs;
	std::vector<ObjCorner> corners;
	std::vector<unsigned> faceStart;   // [Doruk-Aksoy] each face's first corner; a face runs to the next one's, the last to the end
	std::vector<ObjSurface> surfaces;
};

// [Doruk-Aksoy] What a parse left out, for the loader to say once: lines it could not read, faces dropped (a corner past the positions, or under three corners), and texture coordinates past the file's.
struct ObjProblems
{
	int badLines;
	int droppedFaces;
	int badUVs;
};

// [Doruk-Aksoy] Reads `length` bytes of an OBJ file: `v`, `vt`, `f` (any number of corners, `v`, `v/vt`, `v//vn`, `v/vt/vn`, negative indices counted back from the last) and `usemtl`, comments and CRLF; everything else is skipped; false when no face is left.
bool ComputeObjParse(const char *text, size_t length, ObjData &out, ObjProblems &problems);

// [Doruk-Aksoy] A corner as the model draws it, in model space (x, up, y).
struct ObjDrawVertex
{
	float x, y, z;
	float u, v;
};

// [Doruk-Aksoy] Every surface's faces as triangles back to back, the way UZDoom's FOBJModel lays them out: z negated, v flipped, each triangle wound the other way, a quad split 0-1-3 and 1-2-3, a larger face fanned from its first corner, and a corner with no texture coordinate taking the file's first (0, 0 when it has none); surfaceFirst and surfaceCount are each surface's vertices.
void ComputeObjTriangles(const ObjData &data, std::vector<ObjDrawVertex> &out, std::vector<unsigned> &surfaceFirst, std::vector<unsigned> &surfaceCount);

}} // namespace zx::models

#endif // ZX_OBJMODEL_COMPUTE_H
