// [rc4l] Tests for the vertex-layout helpers (hwrender).
#include "gtest/gtest.h"
#include "features/hwrender/computation/vertexformat_compute.h"

using namespace hwrender;

namespace
{
TEST(VertexFormat, AttribSizeByType)
{
	EXPECT_EQ(ComputeAttribSizeBytes({3, AttribType::Float32}), 12); // vec3 position
	EXPECT_EQ(ComputeAttribSizeBytes({2, AttribType::Float32}), 8);  // vec2 uv
	EXPECT_EQ(ComputeAttribSizeBytes({4, AttribType::UNorm8}), 4);   // rgba8 color
}

TEST(VertexFormat, StrideAndOffsetsForTypicalVertex)
{
	// [rc4l] pos(vec3 f32) + uv(vec2 f32) + color(rgba8) = 12 + 8 + 4 = 24, offsets 0/12/20.
	const VertexAttrib layout[] = {
		{3, AttribType::Float32},
		{2, AttribType::Float32},
		{4, AttribType::UNorm8},
	};
	EXPECT_EQ(ComputeVertexStride(layout, 3), 24);

	int offsets[3];
	EXPECT_EQ(ComputeAttribOffsets(layout, 3, offsets), 24);
	EXPECT_EQ(offsets[0], 0);
	EXPECT_EQ(offsets[1], 12);
	EXPECT_EQ(offsets[2], 20);
}

TEST(VertexFormat, EmptyLayoutIsZero)
{
	EXPECT_EQ(ComputeVertexStride(nullptr, 0), 0);
	EXPECT_EQ(ComputeAttribOffsets(nullptr, 0, nullptr), 0);
}
} // namespace
