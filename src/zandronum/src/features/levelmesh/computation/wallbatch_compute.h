// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Wall draw batching: collapse a run of identically-stated walls into one draw.
//
// Measured on Sunder MAP10: ~10,000 draw items per frame for 16,888 vertices, ~4 ms of CPU against
// 1.74 ms of GPU. At roughly 400 ns per item the cost is the per-wall state set + draw call, not the
// geometry -- so the fastest available win is to stop issuing one draw per wall.
//
// Two pieces live here because both are pure and both are easy to get subtly wrong:
//   * whether two walls may share a draw (get this too loose and walls render with the wrong
//     texture, light or fog -- silently, and only on some maps),
//   * the fan-to-triangle-list vertex arithmetic (batched draws cannot use GL_TRIANGLE_FAN, since a
//     fan restarts at its first vertex and there is no primitive restart in this GL floor).
//
// Header-pure and engine-free; gl_walls_draw.cpp / gl_drawinfo.cpp stay thin glue.

#ifndef ZX_WALLBATCH_COMPUTE_H
#define ZX_WALLBATCH_COMPUTE_H

namespace zx { namespace levelmesh {

// Everything about a wall that the draw state depends on. Two walls may share a draw only if every
// field matches, so this deliberately mirrors what GLWall::Draw sets before RenderWall: the
// material and its clamp flags, the light the colormap resolves to, the fog, and the dynamic-light
// index. `type` is included because RENDERWALL_M2SNF takes a different fog path.
struct WallBatchKey
{
	const void   *material;       // FMaterial*, compared by identity
	int           clampFlags;     // GLWall::flags & 3, which SetMaterial consumes
	int           lightLevel;
	int           relLight;
	int           type;           // WallTypes
	unsigned int  lightColor;     // FColormap::LightColor as packed RGBA
	unsigned int  fadeColor;      // FColormap::FadeColor as packed RGBA
	int           desaturation;
	int           blendFactor;
	unsigned int  dynLightIndex;  // UINT_MAX when the pass carries no dynamic lights
	bool          glowing;        // GLWF_GLOW: glow planes and colors are per-wall uniforms
	// [rc4l] The wall overrides the texture mode for its own draw and restores it afterwards --
	// RENDERWALL_M2SNF with GLT_CLAMPY switches to TM_CLAMPY. A batch sets state once and cannot
	// carry a per-wall override, and silently dropping it renders the wall without its clamp.
	bool          ownTextureMode;
};

// May these two walls be drawn together?
//
// Conservative by construction: a glowing wall never batches, because its glow planes and colors are
// per-wall uniforms that a shared draw cannot carry. A wall with no material never batches either --
// the untextured paths (fog boundaries, color layers) each drive their own state.
bool ComputeCanBatch(const WallBatchKey &a, const WallBatchKey &b);

// Vertices needed to express an n-vertex triangle fan as an independent triangle list: 3*(n-2).
// Returns 0 below three vertices, where there is no triangle to draw.
int ComputeFanTriangleVertexCount(int fanVertices);

// Total triangle-list vertices for a batch of fans, so the caller can reserve once. Returns 0 for a
// null or empty array; individual degenerate fans contribute 0 rather than going negative.
long long ComputeBatchVertexCount(const int *fanVertexCounts, int fanCount);

// Index of the i'th triangle-list vertex within a fan, expanding (0,1,2), (0,2,3), (0,3,4)...
// `tri` is the triangle ordinal and `corner` is 0..2. Returns -1 if either is out of range for a fan
// of `fanVertices`, so a caller cannot silently read past the fan.
int ComputeFanTriangleVertex(int fanVertices, int tri, int corner);

}} // namespace zx::levelmesh

#endif // ZX_WALLBATCH_COMPUTE_H
