// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include <limits.h>

#include "features/levelmesh/computation/wallbatch_compute.h"

using namespace zx::levelmesh;

static int dummyMaterialA = 0;
static int dummyMaterialB = 0;

// A key that is safe to batch with a copy of itself.
static WallBatchKey Base()
{
	WallBatchKey k;
	k.material      = &dummyMaterialA;
	k.clampFlags    = 0;
	k.lightLevel    = 160;
	k.relLight      = 0;
	k.type          = 2;
	k.lightColor    = 0xffffffu;
	k.fadeColor     = 0u;
	k.desaturation  = 0;
	k.blendFactor   = 0;
	k.dynLightIndex = UINT_MAX;
	k.glowing       = false;
	k.ownTextureMode = false;
	return k;
}

// ---- the batching predicate ------------------------------------------------

TEST(WallBatch, IdenticalWallsBatch)
{
	EXPECT_TRUE(ComputeCanBatch(Base(), Base()));
}

TEST(WallBatch, GlowingWallsNeverBatch)
{
	WallBatchKey glow = Base();
	glow.glowing = true;
	// Glow planes and colors are per-wall uniforms; sharing a draw would apply one wall's glow to
	// the other. Refused from either side of the comparison.
	EXPECT_FALSE(ComputeCanBatch(glow, Base()));
	EXPECT_FALSE(ComputeCanBatch(Base(), glow));
	EXPECT_FALSE(ComputeCanBatch(glow, glow));
}

TEST(WallBatch, UntexturedWallsNeverBatch)
{
	WallBatchKey none = Base();
	none.material = 0;
	EXPECT_FALSE(ComputeCanBatch(none, Base()));
	EXPECT_FALSE(ComputeCanBatch(Base(), none));
	EXPECT_FALSE(ComputeCanBatch(none, none));
}

TEST(WallBatch, WallsWithTheirOwnTextureModeNeverBatch)
{
	// RENDERWALL_M2SNF + GLT_CLAMPY swaps in TM_CLAMPY for its own draw and restores it after. A
	// batch sets state once, so it cannot carry that -- dropping it silently renders the wall
	// without its clamp, which is exactly the defect this field exists to prevent.
	WallBatchKey own = Base();
	own.ownTextureMode = true;
	EXPECT_FALSE(ComputeCanBatch(own, Base()));
	EXPECT_FALSE(ComputeCanBatch(Base(), own));
	EXPECT_FALSE(ComputeCanBatch(own, own));
}

TEST(WallBatch, DifferentMaterialBreaksTheBatch)
{
	WallBatchKey other = Base();
	other.material = &dummyMaterialB;
	EXPECT_FALSE(ComputeCanBatch(Base(), other));
}

TEST(WallBatch, EveryStateFieldBreaksTheBatch)
{
	// Each field below is state GLWall::Draw sets before RenderWall. If any one stopped being
	// compared, walls would render with a neighbour's light, fog or clamp mode.
	{ WallBatchKey k = Base(); k.clampFlags    = 1;        EXPECT_FALSE(ComputeCanBatch(Base(), k)); }
	{ WallBatchKey k = Base(); k.lightLevel    = 161;      EXPECT_FALSE(ComputeCanBatch(Base(), k)); }
	{ WallBatchKey k = Base(); k.relLight      = 8;        EXPECT_FALSE(ComputeCanBatch(Base(), k)); }
	{ WallBatchKey k = Base(); k.type          = 3;        EXPECT_FALSE(ComputeCanBatch(Base(), k)); }
	{ WallBatchKey k = Base(); k.lightColor    = 0xff0000u;EXPECT_FALSE(ComputeCanBatch(Base(), k)); }
	{ WallBatchKey k = Base(); k.fadeColor     = 0x102030u;EXPECT_FALSE(ComputeCanBatch(Base(), k)); }
	{ WallBatchKey k = Base(); k.desaturation  = 4;        EXPECT_FALSE(ComputeCanBatch(Base(), k)); }
	{ WallBatchKey k = Base(); k.blendFactor   = 2;        EXPECT_FALSE(ComputeCanBatch(Base(), k)); }
	{ WallBatchKey k = Base(); k.dynLightIndex = 7;        EXPECT_FALSE(ComputeCanBatch(Base(), k)); }
}

TEST(WallBatch, MatchingDynamicLightIndexStillBatches)
{
	// Two walls that resolved to the same uploaded light range share their draw legitimately.
	WallBatchKey a = Base(); a.dynLightIndex = 12;
	WallBatchKey b = Base(); b.dynLightIndex = 12;
	EXPECT_TRUE(ComputeCanBatch(a, b));
}

TEST(WallBatch, BatchingIsSymmetric)
{
	WallBatchKey a = Base();
	WallBatchKey b = Base(); b.lightLevel = 200;
	EXPECT_EQ(ComputeCanBatch(a, b), ComputeCanBatch(b, a));
	EXPECT_EQ(ComputeCanBatch(a, a), ComputeCanBatch(a, a));
}

// ---- fan to triangle list --------------------------------------------------

TEST(WallBatch, QuadFanBecomesTwoTriangles)
{
	EXPECT_EQ(ComputeFanTriangleVertexCount(4), 6);
}

TEST(WallBatch, TriangleFanIsOneTriangle)
{
	EXPECT_EQ(ComputeFanTriangleVertexCount(3), 3);
}

TEST(WallBatch, SplitWallFansExpandLinearly)
{
	// Seamless splitting adds vertices to the fan; the triangle count follows n-2.
	EXPECT_EQ(ComputeFanTriangleVertexCount(6), 12);
	EXPECT_EQ(ComputeFanTriangleVertexCount(10), 24);
}

TEST(WallBatch, DegenerateFansProduceNothing)
{
	EXPECT_EQ(ComputeFanTriangleVertexCount(2), 0);
	EXPECT_EQ(ComputeFanTriangleVertexCount(1), 0);
	EXPECT_EQ(ComputeFanTriangleVertexCount(0), 0);
	EXPECT_EQ(ComputeFanTriangleVertexCount(-5), 0);
}

TEST(WallBatch, BatchVertexCountSumsFans)
{
	const int fans[4] = { 4, 4, 6, 3 };
	EXPECT_EQ(ComputeBatchVertexCount(fans, 4), 6 + 6 + 12 + 3);
}

TEST(WallBatch, BatchVertexCountSkipsDegenerateFans)
{
	const int fans[4] = { 4, 2, 0, 4 };
	EXPECT_EQ(ComputeBatchVertexCount(fans, 4), 12);
}

TEST(WallBatch, BatchVertexCountHandlesEmptyInput)
{
	const int one = 4;
	EXPECT_EQ(ComputeBatchVertexCount(0, 3), 0);
	EXPECT_EQ(ComputeBatchVertexCount(&one, 0), 0);
	EXPECT_EQ(ComputeBatchVertexCount(&one, -2), 0);
}

TEST(WallBatch, BatchVertexCountStaysWide)
{
	// A big batch must not wrap a 32-bit accumulator.
	static int fans[4096];
	for (int i = 0; i < 4096; i++) fans[i] = 1002; // 3000 verts each
	EXPECT_EQ(ComputeBatchVertexCount(fans, 4096), 3000LL * 4096LL);
}

// ---- fan expansion order ---------------------------------------------------

TEST(WallBatch, FanExpansionMatchesTriangleFanWinding)
{
	// A 5-vertex fan must expand to (0,1,2) (0,2,3) (0,3,4) -- the same winding GL produces, or
	// batching would flip faces and back-face culling would eat walls that used to draw.
	const int expected[3][3] = { {0,1,2}, {0,2,3}, {0,3,4} };
	for (int t = 0; t < 3; t++)
		for (int c = 0; c < 3; c++)
			EXPECT_EQ(ComputeFanTriangleVertex(5, t, c), expected[t][c]) << "tri " << t << " corner " << c;
}

TEST(WallBatch, FanExpansionRejectsOutOfRange)
{
	EXPECT_EQ(ComputeFanTriangleVertex(4, -1, 0), -1);
	EXPECT_EQ(ComputeFanTriangleVertex(4, 2, 0), -1);  // only 2 triangles in a quad
	EXPECT_EQ(ComputeFanTriangleVertex(4, 0, -1), -1);
	EXPECT_EQ(ComputeFanTriangleVertex(4, 0, 3), -1);
	EXPECT_EQ(ComputeFanTriangleVertex(2, 0, 0), -1);  // no triangles at all
}

TEST(WallBatch, FanExpansionNeverReadsPastTheFan)
{
	// Every index a full expansion produces must be a real vertex of the fan.
	for (int n = 3; n <= 12; n++)
		for (int t = 0; t < n - 2; t++)
			for (int c = 0; c < 3; c++)
			{
				const int v = ComputeFanTriangleVertex(n, t, c);
				EXPECT_GE(v, 0);
				EXPECT_LT(v, n) << "fan " << n << " tri " << t << " corner " << c;
			}
}
