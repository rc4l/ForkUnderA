// [MGOOOOOO] Wireframe geometry for the debug hitbox overlay: turns an actor's collision extents
// (or an explosion's damage region) into a GL_LINES vertex list. Kept free of engine headers so the
// geometry can be unit-tested without linking the game -- the renderer side (features/hitboxviz/
// hitboxviz.cpp) only converts fixed_t to float and copies these into the flat VBO.
//
// Coordinates here are world-space: x/y are the horizontal map axes and z is vertical (up). The
// renderer's FFlatVertex::Set() takes its arguments as (x, vertical, y), so the caller does that
// swap when filling the buffer -- this header deliberately keeps the intuitive naming.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#ifndef ZX_HITBOXVIZ_BOXEDGES_COMPUTE_H
#define ZX_HITBOXVIZ_BOXEDGES_COMPUTE_H

namespace zx { namespace hitboxviz {

struct Vertex3
{
	float x, y, z;
};

enum
{
	// 12 edges of a box, 2 vertices each, emitted as an unindexed GL_LINES list.
	BOX_EDGE_VERTS    = 24,
	// 2 crossing diagonals marking a horizontal plane.
	PLANE_MARKER_VERTS = 4,
};

// Emits the 12 edges of the axis-aligned box centred on (centerX, centerY), spanning
// [bottomZ, bottomZ + height], extending `radius` along both horizontal axes. This is exactly the
// volume the blockmap tests against, so it is built from radius/height (or the attack extent), never
// from sprite bounds.
//
// `out` must have room for BOX_EDGE_VERTS vertices. Returns the number written, or 0 for a
// degenerate box (radius or height <= 0), which writes nothing -- a flat or empty box would render
// as coincident and zero-length edges rather than anything readable.
unsigned int BuildBoxEdges(float centerX, float centerY, float bottomZ, float radius, float height, Vertex3 *out);

// Emits two crossing diagonals across the horizontal plane at `planeZ`, marking a reference height
// inside a box. Used for the explosion origin, which sits at the vertical centre of a blast prism
// and is otherwise impossible to pick out from the symmetric wireframe. `out` must have room for
// PLANE_MARKER_VERTS vertices. Returns the number written, or 0 when radius <= 0.
unsigned int BuildPlaneMarker(float centerX, float centerY, float planeZ, float radius, Vertex3 *out);

// The box parameters of an explosion's damage region, for feeding straight back into BuildBoxEdges.
struct BlastPrism
{
	float centerX, centerY, bottomZ, radius, height;
};

// Derives the region P_RadiusAttack actually tests. The falloff is Chebyshev -- p_map.cpp uses
// `len = max(dx, dy)` under the comment "The damage pattern is square, not circular." -- and the
// vertical reach is the same `distance`, so the region is the cube
// [x +/- distance] x [y +/- distance] x [z - distance, z + distance]. Drawing a sphere here would
// misreport which actors are in range.
BlastPrism ComputeBlastPrism(float x, float y, float z, float distance);

// Mirrors P_RadiusAttack's own clamp of the full-damage radius, so the inner prism can never
// disagree with the simulation that produced it.
int ClampFullDamageDistance(int distance, int fulldamagedistance);

}} // namespace zx::hitboxviz

#endif // ZX_HITBOXVIZ_BOXEDGES_COMPUTE_H
