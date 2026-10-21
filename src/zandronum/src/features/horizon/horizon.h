// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Line_Horizon for the map-driven backend: each horizon sidedef becomes a window onto its planes -- see README.md.

#ifndef ZX_HORIZON_H
#define ZX_HORIZON_H

#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/computation/flatmesh_compute.h"


namespace zx { namespace horizon {

// [rc4l] One plane a window opens onto, shaded and mapped the way GLHorizonPortal draws it.
struct HorizonPlane
{
	bool  valid;
	float z;                             // the plane's texture height
	const void *material;                // FMaterial*, this frame's animation frame
	zx::levelmesh::PlaneUVAffine uv;     // the flat's mapping, evaluated per fragment
	zx::levelmesh::MeshPiece shade;      // CaptureShading's colour, soft light and fog
};

// [rc4l] One side of one horizon line.
struct HorizonWindow
{
	float x1, y1, x2, y2;     // map space, ordered so the front sector is on the right
	float floorZ, ceilingZ;   // what the eye's split is clamped to
	float bottom, top;        // the sky quad's reach -- see ComputeHorizonWindowSpan
	HorizonPlane floor, ceiling;
	bool  skyAbove;           // the ceiling half is sky, drawn by the sky-surface pass
};

// [rc4l] Rebuild this frame's windows from the map; cheap, since a level has a handful.
void UpdateHorizons();

const HorizonWindow *HorizonWindows(int &count);

// [rc4l] The sky halves for an eye at height eyeZ as triangle-list quads in FFlatVertex's (x, height, y), each starting where its floor half stops (ComputeHorizonSkyBottom).
const zx::levelmesh::MeshVertex *HorizonSkyVertices(int &count, float eyeZ);

// [rc4l] Whether this feature owns horizon lines, which the GL sky capture asks before standing down for one.
bool HorizonsActive();

// [rc4l] Whether a line's sky is the horizon's to draw, built for every eye height, and so kept out of the sky list.
bool OwnsSky(int lineSpecial);

}} // namespace zx::horizon

#endif // ZX_HORIZON_H
