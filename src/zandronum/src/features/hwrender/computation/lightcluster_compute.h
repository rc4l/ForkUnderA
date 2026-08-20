// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which lights can possibly reach this pixel -- answered by where it is, not by a list built
// for the surface it sits on.
//
// The engine's answer today is a per-surface light list: walk side_t::lighthead, test each light
// against the wall's plane, upload the survivors, hand the surface an index into that upload. It is
// CPU work proportional to surfaces times lights, it is rebuilt every frame, and the index it
// produces is the single most dangerous value in the renderer -- it outlived its light once already
// and lit a wall from a dead plasma bolt (see walllight_compute.h).
//
// A cluster grid replaces all of it with arithmetic. Space in front of the camera is divided into
// cells -- a screen tile crossed with a depth slice -- and each light is written into the cells its
// sphere touches, once. A fragment computes which cell it is in from its own position and reads that
// cell's short list. No per-surface state, nothing to go stale, and no relationship between a
// surface and a light that has to be maintained.
//
// The depth slices are exponential, not uniform. A uniform split spends most of its cells on distance
// nobody is standing in: Doom's near plane is a few units away and its far plane thousands, so the
// first slice of a uniform grid would swallow every light in the room. The standard mapping (Olsson
// et al., "Clustered Deferred and Forward Shading") gives each slice a constant RATIO of depth, so
// cell size grows with distance the way error tolerance does.
//
// Header-pure and engine-free: no GL, no Diligent, no level structures. Everything here is a
// function of a view-space position, a radius and the grid, which is what makes the boundaries
// testable off-engine -- and boundaries are where every lighting bug in this port has lived.

#ifndef ZX_LIGHTCLUSTER_COMPUTE_H
#define ZX_LIGHTCLUSTER_COMPUTE_H

namespace zx { namespace hwrender {

// The grid, in the only terms it needs.
//
// View space here is right-handed with +z FORWARD, so a point in front of the camera has a positive
// z and it is a distance rather than a signed axis. That differs from GL's clip convention on
// purpose: every comparison below reads as "how far away", and a sign error in a depth slice is
// exactly the kind of mistake that produces lighting which is subtly wrong everywhere.
struct ClusterGrid
{
	int   tilesX, tilesY;   // screen tiles across and down
	int   slices;           // depth slices from zNear to zFar
	float zNear, zFar;      // the depth range the slices cover, in map units

	// The projection, as the two scale factors that turn view space into NDC:
	//   ndcX = projX * x / z,  ndcY = projY * y / z
	// projY = 1 / tan(fovY / 2), projX = projY / aspect.
	float projX, projY;
};

// Cells in the whole grid. Cluster indices run 0..count-1.
int ComputeClusterCount(const ClusterGrid &g);

// The slice a view-space depth falls in, clamped into the grid.
//
// Depth at or before zNear is slice 0 and depth beyond zFar is the last slice, rather than being
// rejected: a light straddling the near plane still lights what is in front of the camera, and a
// fragment fractionally past zFar still has to be lit by something rather than by nothing.
int ComputeSliceForDepth(const ClusterGrid &g, float viewZ);

// The depth range a slice covers. Inverse of ComputeSliceForDepth, and the thing a binning pass
// needs to know how deep a cell reaches.
void ComputeSliceDepthRange(const ClusterGrid &g, int slice, float *outNear, float *outFar);

// The block of cells a light touches. Inclusive on every axis; `empty` when the light is entirely
// behind the camera or entirely outside the frustum.
struct ClusterRange
{
	int  x0, x1;
	int  y0, y1;
	int  z0, z1;
	bool empty;
};

// Which cells a spherical light touches, conservatively.
//
// Conservative in one direction only, and deliberately: a cell wrongly included costs a fragment one
// extra distance test, and a cell wrongly excluded puts a hard unlit edge across a surface -- which
// is precisely the fault the on-plane slack in lightside_compute.h exists to prevent, arriving by a
// different road. The sphere's own axis-aligned box is projected corner by corner rather than
// solving for the exact silhouette, which over-covers slightly and cannot under-cover.
ClusterRange ComputeLightClusters(const ClusterGrid &g, const float viewPos[3], float radius);

// Flatten a cell coordinate. Slice varies slowest, so a fragment's neighbours in screen space are
// neighbours in memory.
int ComputeClusterIndex(const ClusterGrid &g, int tileX, int tileY, int slice);

// The cell a view-space point falls in -- the fragment's side of the same arithmetic. Returns -1 for
// a point behind the camera, which has no cell.
int ComputeClusterForPoint(const ClusterGrid &g, const float viewPos[3]);


// The same question asked with a matrix instead of a basis.
//
// [rc4l] The backend has an MVP and nothing else -- BuildMVP composes the view and the projection
// and keeps only the product -- so binning in view space would mean rebuilding a basis that already
// exists in a matrix, from angles, a second time. Two derivations of the same camera is how the
// yaw error that mirrored the entire world survived for weeks.
//
// Depth here is the clip w, which for this projection is the distance along the view axis. That is
// deliberate and not an implementation detail: the fragment side reads 1.0 / gl_FragCoord.w, which
// is the same number arrived at by the hardware. A cluster the CPU assigns and a cluster the shader
// looks up have to agree exactly, and the surest way to make two sides agree is to have them read
// the same quantity rather than two quantities that ought to be equal.
//
// Column-major, as the backend stores it: m[col * 4 + row].
ClusterRange ComputeLightClustersFromMVP(const ClusterGrid &g, const float mvp[16],
	const float worldPos[3], float radius);

// Tile size in pixels and depth slices, shared by the binning pass and the shader.
//
// Compile-time constants rather than variables because both sides must agree on the grid, and the
// shader -- a GLSL string that cannot read a C++ value -- spells them out. dgscene.cpp asserts the
// two against each other, which only works if these are constant expressions.
enum { kClusterTilePixels = 64, kClusterSlices = 24 };

// The grid a screen of this size gets. One place, so the shader and the binning agree by
// construction rather than by both being edited at the same time.
ClusterGrid ComputeGridForScreen(int screenW, int screenH, float zNear, float zFar);

}} // namespace zx::hwrender

#endif // ZX_LIGHTCLUSTER_COMPUTE_H
