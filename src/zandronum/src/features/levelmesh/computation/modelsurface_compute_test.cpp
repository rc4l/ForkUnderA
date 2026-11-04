// [Doruk-Aksoy] Tests for modelsurface_compute: a model surface's corners lerped, transformed and bounds checked.
#include <gtest/gtest.h>

#include "features/levelmesh/computation/modelsurface_compute.h"

using namespace zx::levelmesh;

namespace {
const float kIdentity[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
// [Doruk-Aksoy] Two keyframes of one triangle, the second 10 units up the first's x.
const float kVerts[] = {
	0, 0, 0, 0.f, 0.f,   1, 0, 0, 1.f, 0.f,   0, 1, 0, 0.f, 1.f,
	10, 0, 0, 0.5f, 0.5f,   11, 0, 0, 0.5f, 0.5f,   10, 1, 0, 0.5f, 0.5f,
};
}

// [Doruk-Aksoy] Without indices the vertices are drawn in order, and each corner keeps its first keyframe's texture coordinate.
TEST(ModelSurface, SequentialCornersComeInOrder)
{
	float out[15];
	ASSERT_TRUE(ComputeModelSurfaceCorners(kVerts, 6, nullptr, 0, { 0, 0, 0.f, 0, 3 }, kIdentity, out));
	EXPECT_FLOAT_EQ(1.f, out[5]);
	EXPECT_FLOAT_EQ(1.f, out[8]);
	EXPECT_FLOAT_EQ(1.f, out[11]);
	EXPECT_FLOAT_EQ(1.f, out[14]);
}

// [Doruk-Aksoy] Indices pick the corners, from firstIndex on.
TEST(ModelSurface, IndicesPickTheCorners)
{
	const unsigned idx[] = { 9, 2, 1, 0 };
	float out[15];
	ASSERT_TRUE(ComputeModelSurfaceCorners(kVerts, 6, idx, 4, { 0, 0, 0.f, 1, 3 }, kIdentity, out));
	EXPECT_FLOAT_EQ(0.f, out[0]);
	EXPECT_FLOAT_EQ(1.f, out[1]);
	EXPECT_FLOAT_EQ(1.f, out[5]);
	EXPECT_FLOAT_EQ(0.f, out[10]);
}

// [Doruk-Aksoy] Two keyframes blend by inter; equal keyframes or a zero inter take the first unblended.
TEST(ModelSurface, KeyframesLerp)
{
	float out[15];
	ASSERT_TRUE(ComputeModelSurfaceCorners(kVerts, 6, nullptr, 0, { 0, 3, 0.25f, 0, 3 }, kIdentity, out));
	EXPECT_FLOAT_EQ(2.5f, out[0]);
	EXPECT_FLOAT_EQ(3.5f, out[5]);
	EXPECT_FLOAT_EQ(0.f, out[3]);
	ASSERT_TRUE(ComputeModelSurfaceCorners(kVerts, 6, nullptr, 0, { 0, 3, 0.f, 0, 3 }, kIdentity, out));
	EXPECT_FLOAT_EQ(0.f, out[0]);
	ASSERT_TRUE(ComputeModelSurfaceCorners(kVerts, 6, nullptr, 0, { 3, 3, 0.5f, 0, 3 }, kIdentity, out));
	EXPECT_FLOAT_EQ(10.f, out[0]);
}

// [Doruk-Aksoy] The column-major matrix's translation and axes land each corner.
TEST(ModelSurface, TheMatrixPlacesTheCorners)
{
	const float m[16] = { 0, 2, 0, 0,  -1, 0, 0, 0,  0, 0, 3, 0,  5, 6, 7, 1 };
	const float v[] = { 1, 2, 3, 0, 0,  0, 0, 0, 0, 0,  0, 0, 0, 0, 0 };
	float out[15];
	ASSERT_TRUE(ComputeModelSurfaceCorners(v, 3, nullptr, 0, { 0, 0, 0.f, 0, 3 }, m, out));
	EXPECT_FLOAT_EQ(3.f, out[0]);
	EXPECT_FLOAT_EQ(8.f, out[1]);
	EXPECT_FLOAT_EQ(16.f, out[2]);
}

// [Doruk-Aksoy] No vertices, under a triangle, an index range past the indices, or an index or keyframe past the vertices is refused.
TEST(ModelSurface, BadSpansAreRefused)
{
	const unsigned idx[] = { 0, 1, 7 };
	float out[15];
	EXPECT_FALSE(ComputeModelSurfaceCorners(nullptr, 6, nullptr, 0, { 0, 0, 0.f, 0, 3 }, kIdentity, out));
	EXPECT_FALSE(ComputeModelSurfaceCorners(kVerts, 6, nullptr, 0, { 0, 0, 0.f, 0, 2 }, kIdentity, out));
	EXPECT_FALSE(ComputeModelSurfaceCorners(kVerts, 6, idx, 3, { 0, 0, 0.f, 1, 3 }, kIdentity, out));
	EXPECT_FALSE(ComputeModelSurfaceCorners(kVerts, 6, idx, 3, { 0, 0, 0.f, 4, 3 }, kIdentity, out));
	EXPECT_FALSE(ComputeModelSurfaceCorners(kVerts, 6, idx, 3, { 0, 0, 0.f, 0, 3 }, kIdentity, out));
	EXPECT_FALSE(ComputeModelSurfaceCorners(kVerts, 6, nullptr, 0, { 0, 4, 0.5f, 0, 3 }, kIdentity, out));
	EXPECT_FALSE(ComputeModelSurfaceCorners(kVerts, 6, nullptr, 0, { 4, 0, 0.5f, 0, 3 }, kIdentity, out));
}
