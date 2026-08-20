// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The boundaries, because every lighting fault in this port has been a boundary.
//
// A light exactly on a plane was dropped and blacked out the floor it was resting on. A light index
// one frame old lit a wall from a bolt that had died. Neither was a hard sum -- both were a case
// nobody wrote down. So the cases are written down here first, before any of this reaches a shader:
// a light behind the camera, a light straddling the near plane, a light exactly on a cell edge, a
// grid with a degenerate depth range, and a fragment past the far plane.
//
// The rule this file is really enforcing is the asymmetry in ComputeLightClusters: over-covering
// costs a distance test, under-covering puts a hard unlit edge across a surface. Every ambiguous
// case below asserts the inclusive answer.

#include <gtest/gtest.h>

#include <math.h>

#include "features/hwrender/computation/lightcluster_compute.h"

using namespace zx::hwrender;

namespace {

// A grid shaped like something the engine would actually use: a 1024x640 window in 16x16 tiles,
// Doom's near plane, and a far plane past the end of any room.
ClusterGrid Grid()
{
	ClusterGrid g;
	g.tilesX = 64; g.tilesY = 40; g.slices = 24;
	g.zNear = 5.f; g.zFar = 8000.f;
	g.projY = 1.f / tanf(45.f * 3.14159265f / 180.f);   // 90 degree vertical fov
	g.projX = g.projY / 1.6f;                            // 1024x640
	return g;
}

const float *At(float x, float y, float z)
{
	static float p[3];
	p[0] = x; p[1] = y; p[2] = z;
	return p;
}

} // namespace

TEST(LightCluster, CountIsTheProduct)
{
	EXPECT_EQ(64 * 40 * 24, ComputeClusterCount(Grid()));
}

// Slices are exponential: each covers a constant RATIO of depth, so the first few are small and the
// last is enormous. A uniform split would put every light in a room into slice 0.
TEST(LightCluster, SlicesAreExponential)
{
	const ClusterGrid g = Grid();
	float n0, f0, n1, f1, nLast, fLast;
	ComputeSliceDepthRange(g, 0, &n0, &f0);
	ComputeSliceDepthRange(g, 1, &n1, &f1);
	ComputeSliceDepthRange(g, g.slices - 1, &nLast, &fLast);

	EXPECT_FLOAT_EQ(g.zNear, n0);
	EXPECT_NEAR(f0, n1, 0.001f);          // slices meet, no gap
	EXPECT_NEAR(g.zFar, fLast, 0.5f);     // and the last one ends at the far plane
	EXPECT_GT(fLast - nLast, f0 - n0);    // and it is far deeper than the first
}

// Round-tripping a depth through the slice it lands in has to give that slice back.
TEST(LightCluster, DepthRoundTripsThroughItsSlice)
{
	const ClusterGrid g = Grid();
	for (int s = 0; s < g.slices; s++)
	{
		float n, f;
		ComputeSliceDepthRange(g, s, &n, &f);
		const float middle = sqrtf(n * f);   // geometric, because the mapping is
		EXPECT_EQ(s, ComputeSliceForDepth(g, middle)) << "slice " << s;
	}
}

TEST(LightCluster, DepthOutsideTheRangeClampsRatherThanEscaping)
{
	const ClusterGrid g = Grid();
	EXPECT_EQ(0, ComputeSliceForDepth(g, g.zNear));
	EXPECT_EQ(0, ComputeSliceForDepth(g, g.zNear * 0.5f));
	EXPECT_EQ(0, ComputeSliceForDepth(g, 0.f));
	// A fragment past the far plane still has to be lit by something.
	EXPECT_EQ(g.slices - 1, ComputeSliceForDepth(g, g.zFar));
	EXPECT_EQ(g.slices - 1, ComputeSliceForDepth(g, g.zFar * 10.f));
}

// A degenerate grid must not produce a NaN index. floorf(NaN) is not a number either, and the clamp
// that catches everything else does not catch that.
TEST(LightCluster, DegenerateRangeDoesNotProduceNonsense)
{
	ClusterGrid g = Grid();
	g.zFar = g.zNear;
	EXPECT_EQ(0, ComputeSliceForDepth(g, 100.f));
	g.zNear = 0.f; g.zFar = 100.f;
	EXPECT_EQ(0, ComputeSliceForDepth(g, 50.f));
}

// A point dead ahead is in the middle of the screen. With an even tile count the middle falls on the
// boundary between two tiles, and floor puts it in the upper one -- stated here so a later change to
// the rounding is a failing test rather than a shifted light.
TEST(LightCluster, PointOnTheViewAxisLandsInTheMiddle)
{
	const ClusterGrid g = Grid();
	const int c = ComputeClusterForPoint(g, At(0.f, 0.f, 100.f));
	ASSERT_GE(c, 0);
	const int slice = c / (g.tilesX * g.tilesY);
	const int ty = (c - slice * g.tilesX * g.tilesY) / g.tilesX;
	const int tx = c % g.tilesX;
	EXPECT_EQ(g.tilesX / 2, tx);
	EXPECT_EQ(g.tilesY / 2, ty);
	EXPECT_EQ(ComputeSliceForDepth(g, 100.f), slice);
}

// Behind the camera has no cell at all. Clamping it into slice 0 would light the room in front from
// a light that is behind you.
TEST(LightCluster, BehindTheCameraHasNoCell)
{
	const ClusterGrid g = Grid();
	EXPECT_EQ(-1, ComputeClusterForPoint(g, At(0.f, 0.f, -10.f)));
	EXPECT_EQ(-1, ComputeClusterForPoint(g, At(0.f, 0.f, 0.f)));
}

// A light entirely behind the camera touches nothing.
TEST(LightCluster, LightBehindTheCameraIsEmpty)
{
	const ClusterGrid g = Grid();
	EXPECT_TRUE(ComputeLightClusters(g, At(0.f, 0.f, -200.f), 64.f).empty);
}

// ...but one that merely REACHES past the camera is not. This is the plasma bolt flying past the
// player's ear: its sphere crosses the near plane, and it still lights the wall in front.
TEST(LightCluster, LightStraddlingTheNearPlaneCoversTheScreen)
{
	const ClusterGrid g = Grid();
	const ClusterRange r = ComputeLightClusters(g, At(0.f, 0.f, 10.f), 200.f);
	ASSERT_FALSE(r.empty);
	EXPECT_EQ(0, r.x0);
	EXPECT_EQ(g.tilesX - 1, r.x1);
	EXPECT_EQ(0, r.y0);
	EXPECT_EQ(g.tilesY - 1, r.y1);
	EXPECT_EQ(0, r.z0);
}

// A small light far off to the side is not everybody's problem: it must NOT cover the whole grid, or
// clustering has bought nothing.
TEST(LightCluster, ASmallLightIsLocal)
{
	const ClusterGrid g = Grid();
	const ClusterRange r = ComputeLightClusters(g, At(300.f, 0.f, 1000.f), 64.f);
	ASSERT_FALSE(r.empty);
	const int tiles = (r.x1 - r.x0 + 1) * (r.y1 - r.y0 + 1);
	EXPECT_LT(tiles, g.tilesX * g.tilesY / 4) << "a 64-unit light 1000 units away covered a quarter of the screen";
	EXPECT_GE(r.x0, 0);
	EXPECT_LE(r.x1, g.tilesX - 1);
}

// Far off the side of the screen entirely: nothing.
TEST(LightCluster, LightOffScreenIsEmpty)
{
	const ClusterGrid g = Grid();
	EXPECT_TRUE(ComputeLightClusters(g, At(20000.f, 0.f, 1000.f), 64.f).empty);
}

// The asymmetry, stated as a test: a light whose edge sits exactly on a cell boundary must be in
// BOTH cells. Under-covering is what puts a hard unlit line across a floor -- the same fault shape
// as the on-plane cull in lightside_compute.h, arriving by a different road.
TEST(LightCluster, ALightOnACellEdgeIsInBothCells)
{
	const ClusterGrid g = Grid();
	// Put the light's centre exactly on the boundary between two depth slices.
	float n, f;
	ComputeSliceDepthRange(g, 8, &n, &f);
	const ClusterRange r = ComputeLightClusters(g, At(0.f, 0.f, f), 1.f);
	ASSERT_FALSE(r.empty);
	EXPECT_LE(r.z0, 8);
	EXPECT_GE(r.z1, 8);
	EXPECT_GE(r.z1, r.z0);
}

// Every cell a light claims must be a real index, for every light in a sweep across the frustum.
// An out-of-range cluster index is a buffer overrun in the binning pass, which is the one failure
// here that does not merely look wrong.
TEST(LightCluster, EveryClaimedCellIsInRange)
{
	const ClusterGrid g = Grid();
	const int count = ComputeClusterCount(g);
	for (int i = 0; i < 200; i++)
	{
		const float x = -2000.f + 20.f * (float)i;
		const float z = 5.f + 40.f * (float)i;
		const ClusterRange r = ComputeLightClusters(g, At(x, x * 0.5f, z), 32.f + (float)i);
		if (r.empty) continue;
		EXPECT_GE(r.x0, 0); EXPECT_LT(r.x1, g.tilesX);
		EXPECT_GE(r.y0, 0); EXPECT_LT(r.y1, g.tilesY);
		EXPECT_GE(r.z0, 0); EXPECT_LT(r.z1, g.slices);
		EXPECT_LE(r.x0, r.x1);
		EXPECT_LE(r.y0, r.y1);
		EXPECT_LE(r.z0, r.z1);
		EXPECT_GE(ComputeClusterIndex(g, r.x0, r.y0, r.z0), 0);
		EXPECT_LT(ComputeClusterIndex(g, r.x1, r.y1, r.z1), count);
	}
}

// The property that makes the whole thing work: a point inside a light's radius must be in a cell
// that light claimed. If this can fail, a surface goes dark next to one that did not.
TEST(LightCluster, APointInsideTheLightIsInACellTheLightClaimed)
{
	const ClusterGrid g = Grid();
	const float radius = 150.f;
	const float lp[3] = { 60.f, -40.f, 700.f };
	const ClusterRange r = ComputeLightClusters(g, lp, radius);
	ASSERT_FALSE(r.empty);

	for (int i = 0; i < 64; i++)
	{
		// A spread of points inside the sphere, including its extremes along each axis.
		const float a = (float)i * 0.7f;
		const float d = radius * 0.99f * ((i % 4) ? 0.5f : 1.0f);
		const float p[3] = { lp[0] + d * cosf(a), lp[1] + d * sinf(a), lp[2] + d * cosf(a * 1.3f) };
		if (p[2] <= 0.f) continue;

		const int cell = ComputeClusterForPoint(g, p);
		ASSERT_GE(cell, 0);
		const int slice = cell / (g.tilesX * g.tilesY);
		const int ty = (cell - slice * g.tilesX * g.tilesY) / g.tilesX;
		const int tx = cell % g.tilesX;
		EXPECT_GE(tx, r.x0); EXPECT_LE(tx, r.x1);
		EXPECT_GE(ty, r.y0); EXPECT_LE(ty, r.y1);
		EXPECT_GE(slice, r.z0); EXPECT_LE(slice, r.z1);
	}
}

// ---------------------------------------------------------------------------------------------
// The matrix path, which is the one the backend actually calls.
// ---------------------------------------------------------------------------------------------

namespace {

// The backend's own projection, built the way BuildMVP builds it: column-major, w_clip = -z_eye,
// Vulkan depth. With an identity view this makes world space and eye space the same thing, so the
// two binning paths can be compared directly.
void Projection(float *m, float fovY, float aspect, float zn, float zf)
{
	for (int i = 0; i < 16; i++) m[i] = 0.f;
	const float f = 1.0f / tanf(fovY * 0.5f);
	m[0] = f / aspect; m[5] = f; m[10] = zf / (zn - zf); m[11] = -1.0f;
	m[14] = (zf * zn) / (zn - zf);
}

} // namespace

TEST(LightCluster, GridForScreenCoversIt)
{
	const ClusterGrid g = ComputeGridForScreen(1024, 640, 5.f, 8000.f);
	EXPECT_EQ(1024 / kClusterTilePixels, g.tilesX);
	EXPECT_EQ(640 / kClusterTilePixels, g.tilesY);
	EXPECT_EQ(kClusterSlices, g.slices);
	// A screen that does not divide evenly still has to be covered to its last pixel.
	const ClusterGrid odd = ComputeGridForScreen(1000, 601, 5.f, 8000.f);
	EXPECT_GE(odd.tilesX * kClusterTilePixels, 1000);
	EXPECT_GE(odd.tilesY * kClusterTilePixels, 601);
}

// The two paths are the same question in two coordinate systems, and they have to answer it the
// same way. This is the test that catches a transposed matrix or a flipped sign -- the failure mode
// that mirrored the entire world once already, and which no average-colour comparison can see.
TEST(LightCluster, MatrixPathAgreesWithTheViewSpacePath)
{
	ClusterGrid g = ComputeGridForScreen(1024, 640, 5.f, 8000.f);
	const float fovY = 74.f * 3.14159265f / 180.f;
	const float aspect = 1024.f / 640.f;
	g.projY = 1.f / tanf(fovY * 0.5f);
	g.projX = g.projY / aspect;

	float mvp[16];
	Projection(mvp, fovY, aspect, 5.f, 65536.f);

	int compared = 0;
	for (int i = 1; i < 40; i++)
	{
		const float depth = 20.f + 60.f * (float)i;
		const float x = -400.f + 25.f * (float)i;
		const float y = 150.f - 9.f * (float)i;
		const float radius = 40.f + 6.f * (float)i;

		const float viewPos[3] = { x, y, depth };          // +z forward
		const float worldPos[3] = { x, y, -depth };        // eye space, -z forward
		const ClusterRange a = ComputeLightClusters(g, viewPos, radius);
		const ClusterRange b = ComputeLightClustersFromMVP(g, mvp, worldPos, radius);
		ASSERT_EQ(a.empty, b.empty) << "depth " << depth;
		if (a.empty) continue;
		compared++;
		EXPECT_EQ(a.x0, b.x0) << "depth " << depth;
		EXPECT_EQ(a.x1, b.x1) << "depth " << depth;
		EXPECT_EQ(a.y0, b.y0) << "depth " << depth;
		EXPECT_EQ(a.y1, b.y1) << "depth " << depth;
		EXPECT_EQ(a.z0, b.z0) << "depth " << depth;
		EXPECT_EQ(a.z1, b.z1) << "depth " << depth;
	}
	EXPECT_GT(compared, 20) << "the sweep rejected almost everything: it is not testing what it claims";
}

TEST(LightCluster, MatrixPathPutsBehindTheCameraNowhere)
{
	const ClusterGrid g = ComputeGridForScreen(1024, 640, 5.f, 8000.f);
	float mvp[16];
	Projection(mvp, 74.f * 3.14159265f / 180.f, 1.6f, 5.f, 65536.f);
	const float behind[3] = { 0.f, 0.f, 300.f };   // +z is BEHIND in eye space
	EXPECT_TRUE(ComputeLightClustersFromMVP(g, mvp, behind, 50.f).empty);
}

// The plasma bolt going past your ear. Its box crosses the camera plane, so a projected corner would
// come back mirrored; the answer is every tile, not a mirrored rectangle behind the player.
TEST(LightCluster, MatrixPathGivesAStraddlingLightTheWholeScreen)
{
	const ClusterGrid g = ComputeGridForScreen(1024, 640, 5.f, 8000.f);
	float mvp[16];
	Projection(mvp, 74.f * 3.14159265f / 180.f, 1.6f, 5.f, 65536.f);
	const float atEar[3] = { 0.f, 0.f, -10.f };
	const ClusterRange r = ComputeLightClustersFromMVP(g, mvp, atEar, 200.f);
	ASSERT_FALSE(r.empty);
	EXPECT_EQ(0, r.x0);
	EXPECT_EQ(g.tilesX - 1, r.x1);
	EXPECT_EQ(0, r.y0);
	EXPECT_EQ(g.tilesY - 1, r.y1);
	EXPECT_EQ(0, r.z0);
}

// And it must still be local when it is not straddling, or the grid buys nothing on a map full of
// lights -- which is the entire point of the phase.
TEST(LightCluster, MatrixPathKeepsASmallLightLocal)
{
	const ClusterGrid g = ComputeGridForScreen(1024, 640, 5.f, 8000.f);
	float mvp[16];
	Projection(mvp, 74.f * 3.14159265f / 180.f, 1.6f, 5.f, 65536.f);
	const float p[3] = { 200.f, 0.f, -1200.f };
	const ClusterRange r = ComputeLightClustersFromMVP(g, mvp, p, 64.f);
	ASSERT_FALSE(r.empty);
	const int cells = (r.x1 - r.x0 + 1) * (r.y1 - r.y0 + 1) * (r.z1 - r.z0 + 1);
	EXPECT_LT(cells, ComputeClusterCount(g) / 20);
}

// [rc4l] The property that the near-plane clamp must not break.
//
// The straddling case used to bail out and hand the light every tile, which cannot under-cover
// because it covers everything. Clamping each corner to the near plane instead is eight times
// cheaper and no longer obviously safe -- so it gets the same test the view-space path has, aimed
// squarely at the lights that cross the camera plane: every point inside the sphere must land in a
// cell the sphere claimed. A failure here is a surface going dark next to one that did not.
TEST(LightCluster, MatrixPathCoversPointsInsideAStraddlingLight)
{
	const ClusterGrid g = ComputeGridForScreen(1024, 640, 5.f, 8192.f);
	float mvp[16];
	Projection(mvp, 74.f * 3.14159265f / 180.f, 1.6f, 5.f, 65536.f);

	// Distances that put the sphere across the near plane, just inside it, and clear of it.
	const float depths[] = { 8.f, 40.f, 90.f, 120.f, 400.f };
	const float radius = 96.f;
	for (int d = 0; d < 5; d++)
	{
		const float lp[3] = { 30.f, -20.f, -depths[d] };
		const ClusterRange r = ComputeLightClustersFromMVP(g, mvp, lp, radius);
		ASSERT_FALSE(r.empty) << "depth " << depths[d];

		for (int i = 0; i < 200; i++)
		{
			const float a = (float)i * 0.61f, b = (float)i * 1.37f;
			const float k = radius * 0.99f * ((i % 3) ? 0.6f : 1.0f);
			const float p[3] = { lp[0] + k * cosf(a), lp[1] + k * sinf(b), lp[2] + k * cosf(b) };

			const float w  = mvp[3] * p[0] + mvp[7] * p[1] + mvp[11] * p[2] + mvp[15];
			if (w <= g.zNear) continue;   // in front of the near plane: nothing is shaded there
			const float ndcX = (mvp[0] * p[0] + mvp[4] * p[1] + mvp[8] * p[2] + mvp[12]) / w;
			const float ndcY = (mvp[1] * p[0] + mvp[5] * p[1] + mvp[9] * p[2] + mvp[13]) / w;
			if (ndcX < -1.f || ndcX > 1.f || ndcY < -1.f || ndcY > 1.f) continue;   // off screen

			const int tx = (int)floorf((ndcX * 0.5f + 0.5f) * (float)g.tilesX);
			const int ty = (int)floorf((ndcY * 0.5f + 0.5f) * (float)g.tilesY);
			const int slice = ComputeSliceForDepth(g, w);
			EXPECT_GE(tx, r.x0) << "depth " << depths[d]; EXPECT_LE(tx, r.x1) << "depth " << depths[d];
			EXPECT_GE(ty, r.y0) << "depth " << depths[d]; EXPECT_LE(ty, r.y1) << "depth " << depths[d];
			EXPECT_GE(slice, r.z0) << "depth " << depths[d]; EXPECT_LE(slice, r.z1) << "depth " << depths[d];
		}
	}
}
