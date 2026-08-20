// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/lightcluster_compute.h"

#include <math.h>

namespace zx { namespace hwrender {

namespace {

int Clamp(int v, int lo, int hi)
{
	return (v < lo) ? lo : ((v > hi) ? hi : v);
}

float Ratio(const ClusterGrid &g)
{
	// [rc4l] A degenerate range would put a log of zero or one in the denominator, and the result
	// would be an index of NaN -- which clamps to nothing sensible and lights the world at random.
	if (g.zFar <= g.zNear || g.zNear <= 0.f) return 0.f;
	return logf(g.zFar / g.zNear);
}

} // namespace

int ComputeClusterCount(const ClusterGrid &g)
{
	if (g.tilesX <= 0 || g.tilesY <= 0 || g.slices <= 0) return 0;
	return g.tilesX * g.tilesY * g.slices;
}

int ComputeSliceForDepth(const ClusterGrid &g, float viewZ)
{
	if (g.slices <= 1) return 0;
	const float r = Ratio(g);
	if (r <= 0.f) return 0;
	if (viewZ <= g.zNear) return 0;

	// slice = floor( log(z / zNear) / log(zFar / zNear) * slices )
	const float t = logf(viewZ / g.zNear) / r;
	return Clamp((int)floorf(t * (float)g.slices), 0, g.slices - 1);
}

void ComputeSliceDepthRange(const ClusterGrid &g, int slice, float *outNear, float *outFar)
{
	const float r = Ratio(g);
	if (g.slices <= 0 || r <= 0.f)
	{
		if (outNear) *outNear = g.zNear;
		if (outFar)  *outFar  = g.zFar;
		return;
	}
	const int s = Clamp(slice, 0, g.slices - 1);
	if (outNear) *outNear = g.zNear * expf(r * ((float)s / (float)g.slices));
	if (outFar)  *outFar  = g.zNear * expf(r * ((float)(s + 1) / (float)g.slices));
}

int ComputeClusterIndex(const ClusterGrid &g, int tileX, int tileY, int slice)
{
	if (ComputeClusterCount(g) <= 0) return -1;
	const int x = Clamp(tileX, 0, g.tilesX - 1);
	const int y = Clamp(tileY, 0, g.tilesY - 1);
	const int z = Clamp(slice, 0, g.slices - 1);
	return (z * g.tilesY + y) * g.tilesX + x;
}

int ComputeClusterForPoint(const ClusterGrid &g, const float viewPos[3])
{
	if (ComputeClusterCount(g) <= 0) return -1;
	const float z = viewPos[2];
	// [rc4l] Behind the camera has no cell. Not clamped to slice 0: a point behind the viewer is not
	// "very close", and pretending it is would have the room behind you lit by the cell in front.
	if (z <= 0.f) return -1;

	const float ndcX = g.projX * viewPos[0] / z;
	const float ndcY = g.projY * viewPos[1] / z;
	// NDC -1..1 to tile 0..tiles-1. Clamped rather than rejected: a fragment slightly outside the
	// grid is a rounding artefact at the screen edge, and it still has to be lit.
	const int tx = Clamp((int)floorf((ndcX * 0.5f + 0.5f) * (float)g.tilesX), 0, g.tilesX - 1);
	const int ty = Clamp((int)floorf((ndcY * 0.5f + 0.5f) * (float)g.tilesY), 0, g.tilesY - 1);
	return ComputeClusterIndex(g, tx, ty, ComputeSliceForDepth(g, z));
}

ClusterRange ComputeLightClusters(const ClusterGrid &g, const float viewPos[3], float radius)
{
	ClusterRange out;
	out.x0 = out.y0 = out.z0 = 0;
	out.x1 = out.y1 = out.z1 = 0;
	out.empty = true;
	if (ComputeClusterCount(g) <= 0 || radius <= 0.f) return out;

	const float zMin = viewPos[2] - radius;
	const float zMax = viewPos[2] + radius;
	// Entirely behind the camera: nothing it can reach is on screen.
	if (zMax <= 0.f) return out;

	// [rc4l] The near clamp is what makes a light straddling the camera plane behave.
	//
	// Dividing by a z at or below zero mirrors the projection and puts the light's box on the far
	// side of the screen -- so a plasma bolt passing the camera would light the wall behind the
	// player instead of the one in front. Clamping the near edge to the near plane instead makes such
	// a light cover the whole screen at that depth, which is both correct and what it looks like.
	const float nearZ = (zMin < g.zNear) ? g.zNear : zMin;
	const float farZ  = (zMax < g.zNear) ? g.zNear : zMax;

	// Project the eight corners of the sphere's axis-aligned box. Conservative by construction, and
	// it needs no case analysis for a box that crosses the view axis -- which the exact silhouette
	// solution does, and gets wrong quietly.
	float minNdcX = 1e30f, maxNdcX = -1e30f, minNdcY = 1e30f, maxNdcY = -1e30f;
	for (int i = 0; i < 8; i++)
	{
		const float cx = viewPos[0] + ((i & 1) ? radius : -radius);
		const float cy = viewPos[1] + ((i & 2) ? radius : -radius);
		const float cz = (i & 4) ? farZ : nearZ;
		const float ndcX = g.projX * cx / cz;
		const float ndcY = g.projY * cy / cz;
		if (ndcX < minNdcX) minNdcX = ndcX;
		if (ndcX > maxNdcX) maxNdcX = ndcX;
		if (ndcY < minNdcY) minNdcY = ndcY;
		if (ndcY > maxNdcY) maxNdcY = ndcY;
	}

	// Off the side of the screen entirely.
	if (maxNdcX < -1.f || minNdcX > 1.f || maxNdcY < -1.f || minNdcY > 1.f) return out;

	out.x0 = Clamp((int)floorf((minNdcX * 0.5f + 0.5f) * (float)g.tilesX), 0, g.tilesX - 1);
	out.x1 = Clamp((int)floorf((maxNdcX * 0.5f + 0.5f) * (float)g.tilesX), 0, g.tilesX - 1);
	out.y0 = Clamp((int)floorf((minNdcY * 0.5f + 0.5f) * (float)g.tilesY), 0, g.tilesY - 1);
	out.y1 = Clamp((int)floorf((maxNdcY * 0.5f + 0.5f) * (float)g.tilesY), 0, g.tilesY - 1);
	out.z0 = ComputeSliceForDepth(g, nearZ);
	out.z1 = ComputeSliceForDepth(g, farZ);
	out.empty = false;
	return out;
}

}} // namespace zx::hwrender
