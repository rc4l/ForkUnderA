// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/wallbatch_compute.h"

namespace zx { namespace levelmesh {

bool ComputeCanBatch(const WallBatchKey &a, const WallBatchKey &b)
{
	// [rc4l] Glow planes and glow colors are per-wall uniforms, so two glowing walls cannot share a
	// draw even when everything else matches. Cheapest test, so it goes first.
	if (a.glowing || b.glowing) return false;

	// [rc4l] An untextured wall drives its own state (fog boundary, color layer, mirror surface);
	// batching those would drop the very state that distinguishes them.
	if (a.material == 0 || b.material == 0) return false;

	// [rc4l] Same reasoning for a per-wall texture-mode override: a shared draw has one mode.
	if (a.ownTextureMode || b.ownTextureMode) return false;

	return a.material      == b.material
		&& a.clampFlags    == b.clampFlags
		&& a.lightLevel    == b.lightLevel
		&& a.relLight      == b.relLight
		&& a.type          == b.type
		&& a.lightColor    == b.lightColor
		&& a.fadeColor     == b.fadeColor
		&& a.desaturation  == b.desaturation
		&& a.blendFactor   == b.blendFactor
		&& a.dynLightIndex == b.dynLightIndex;
}

int ComputeFanTriangleVertexCount(int fanVertices)
{
	if (fanVertices < 3) return 0;
	return 3 * (fanVertices - 2);
}

long long ComputeBatchVertexCount(const int *fanVertexCounts, int fanCount)
{
	if (fanVertexCounts == 0 || fanCount <= 0) return 0;

	long long total = 0;
	for (int i = 0; i < fanCount; i++)
		total += ComputeFanTriangleVertexCount(fanVertexCounts[i]);
	return total;
}

int ComputeFanTriangleVertex(int fanVertices, int tri, int corner)
{
	if (fanVertices < 3) return -1;
	if (tri < 0 || tri >= fanVertices - 2) return -1;
	if (corner < 0 || corner > 2) return -1;

	// [rc4l] Fan triangle t is (v0, v[t+1], v[t+2]) -- the same winding GL_TRIANGLE_FAN produces,
	// so batching cannot flip a face and start back-face culling geometry that used to draw.
	if (corner == 0) return 0;
	return tri + corner;
}

}} // namespace zx::levelmesh
