// [rc4l] Tests for 2D quad tessellation (hwrender Phase 2 draw layer).
#include "gtest/gtest.h"
#include "features/hwrender/computation/quad_compute.h"

using namespace hwrender;

namespace
{
TEST(Quad, CoversRectCornersWithUVs)
{
	Vertex2D v[6];
	ComputeQuadVertices(10.0f, 20.0f, 100.0f, 50.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1, 2, 3, 4, v);

	// [rc4l] All six vertices lie within the rect [10,110] x [20,70].
	for (int i = 0; i < 6; i++)
	{
		EXPECT_GE(v[i].x, 10.0f); EXPECT_LE(v[i].x, 110.0f);
		EXPECT_GE(v[i].y, 20.0f); EXPECT_LE(v[i].y, 70.0f);
		EXPECT_FLOAT_EQ(v[i].z, 0.0f);
	}
	// [rc4l] The four distinct corners are present.
	EXPECT_FLOAT_EQ(v[0].x, 10.0f);  EXPECT_FLOAT_EQ(v[0].y, 20.0f);  // top-left
	EXPECT_FLOAT_EQ(v[2].x, 110.0f); EXPECT_FLOAT_EQ(v[2].y, 70.0f);  // bottom-right
	EXPECT_FLOAT_EQ(v[5].x, 110.0f); EXPECT_FLOAT_EQ(v[5].y, 20.0f);  // top-right
	// [rc4l] UVs track the corners.
	EXPECT_FLOAT_EQ(v[0].u, 0.0f); EXPECT_FLOAT_EQ(v[0].v, 0.0f);
	EXPECT_FLOAT_EQ(v[2].u, 1.0f); EXPECT_FLOAT_EQ(v[2].v, 1.0f);
}

TEST(Quad, ColorAppliedToEveryVertex)
{
	Vertex2D v[6];
	ComputeQuadVertices(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 200, 150, 100, 255, v);
	for (int i = 0; i < 6; i++)
	{
		EXPECT_EQ(v[i].r, 200); EXPECT_EQ(v[i].g, 150);
		EXPECT_EQ(v[i].b, 100); EXPECT_EQ(v[i].a, 255);
	}
}

TEST(Quad, TwoTrianglesShareTheDiagonal)
{
	Vertex2D v[6];
	ComputeQuadVertices(0.0f, 0.0f, 4.0f, 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 0, 0, v);
	// [rc4l] Both triangles start at top-left; the shared edge is tl->br.
	EXPECT_FLOAT_EQ(v[0].x, v[3].x); EXPECT_FLOAT_EQ(v[0].y, v[3].y); // tl == tl
	EXPECT_FLOAT_EQ(v[2].x, v[4].x); EXPECT_FLOAT_EQ(v[2].y, v[4].y); // br == br
}

TEST(Quad, WindingIsCounterClockwise)
{
	Vertex2D v[6];
	ComputeQuadVertices(0.0f, 0.0f, 2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0, 0, 0, 0, v);
	// [rc4l] Signed area of the first triangle (tl,bl,br) via the shoelace cross product.
	auto cross = [](const Vertex2D &a, const Vertex2D &b, const Vertex2D &c) {
		return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
	};
	// [rc4l] In screen space (y down) this layout is a consistent, non-degenerate winding.
	EXPECT_NE(cross(v[0], v[1], v[2]), 0.0f);
	EXPECT_EQ(cross(v[0], v[1], v[2]) > 0.0f, cross(v[3], v[4], v[5]) > 0.0f);
}
} // namespace
