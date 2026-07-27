// [rc4l] Tests for the pure 4x4 matrix helpers (hwrender). Column-major layout: m[c*4 + r].
#include "gtest/gtest.h"
#include "features/hwrender/computation/matrix_compute.h"
#include <cmath>

using namespace hwrender;

namespace
{
constexpr float kEps = 1e-5f;

void ExpectMatEq(const Mat4 &a, const Mat4 &b)
{
	for (int i = 0; i < 16; i++)
		EXPECT_NEAR(a.m[i], b.m[i], kEps) << "element " << i;
}

// [rc4l] Multiply a column-major matrix by a column vector (w included).
void Apply(const Mat4 &mat, float x, float y, float z, float w, float out[4])
{
	for (int row = 0; row < 4; row++)
		out[row] = mat.m[0 * 4 + row] * x + mat.m[1 * 4 + row] * y +
		           mat.m[2 * 4 + row] * z + mat.m[3 * 4 + row] * w;
}

TEST(Matrix, IdentityIsDiagonalOnes)
{
	Mat4 id = ComputeIdentity();
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
			EXPECT_FLOAT_EQ(id.m[c * 4 + r], (c == r) ? 1.0f : 0.0f);
}

TEST(Matrix, MultiplyByIdentityIsNoOp)
{
	Mat4 id = ComputeIdentity();
	Mat4 a = ComputeTranslation(3.0f, -4.0f, 5.0f);
	ExpectMatEq(ComputeMultiply(a, id), a);
	ExpectMatEq(ComputeMultiply(id, a), a);
}

TEST(Matrix, MultiplyComposesTransforms)
{
	// [rc4l] (Translate * Scale) applied to a point scales then translates.
	Mat4 m = ComputeMultiply(ComputeTranslation(1.0f, 2.0f, 3.0f), ComputeScale(2.0f, 2.0f, 2.0f));
	float out[4];
	Apply(m, 1.0f, 1.0f, 1.0f, 1.0f, out);
	EXPECT_NEAR(out[0], 3.0f, kEps); // 1*2 + 1
	EXPECT_NEAR(out[1], 4.0f, kEps); // 1*2 + 2
	EXPECT_NEAR(out[2], 5.0f, kEps); // 1*2 + 3
	EXPECT_NEAR(out[3], 1.0f, kEps);
}

TEST(Matrix, TransposeSwapsRowsAndCols)
{
	Mat4 a = ComputeTranslation(7.0f, 8.0f, 9.0f);
	Mat4 t = ComputeTranspose(a);
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
			EXPECT_FLOAT_EQ(t.m[c * 4 + r], a.m[r * 4 + c]);
	// [rc4l] Transpose is an involution.
	ExpectMatEq(ComputeTranspose(t), a);
}

TEST(Matrix, TranslationMovesPoint)
{
	Mat4 m = ComputeTranslation(10.0f, 20.0f, 30.0f);
	float out[4];
	Apply(m, 1.0f, 2.0f, 3.0f, 1.0f, out);
	EXPECT_NEAR(out[0], 11.0f, kEps);
	EXPECT_NEAR(out[1], 22.0f, kEps);
	EXPECT_NEAR(out[2], 33.0f, kEps);
	// [rc4l] A direction (w=0) is unaffected by translation.
	Apply(m, 1.0f, 0.0f, 0.0f, 0.0f, out);
	EXPECT_NEAR(out[0], 1.0f, kEps);
}

TEST(Matrix, ScaleScalesPoint)
{
	Mat4 m = ComputeScale(2.0f, 3.0f, 4.0f);
	float out[4];
	Apply(m, 1.0f, 1.0f, 1.0f, 1.0f, out);
	EXPECT_NEAR(out[0], 2.0f, kEps);
	EXPECT_NEAR(out[1], 3.0f, kEps);
	EXPECT_NEAR(out[2], 4.0f, kEps);
	EXPECT_NEAR(out[3], 1.0f, kEps);
}

TEST(Matrix, RotationZeroAxisIsIdentity)
{
	ExpectMatEq(ComputeRotation(90.0f, 0.0f, 0.0f, 0.0f), ComputeIdentity());
}

TEST(Matrix, Rotation90AboutZ)
{
	// [rc4l] +90 about Z maps +X -> +Y.
	Mat4 m = ComputeRotation(90.0f, 0.0f, 0.0f, 1.0f);
	float out[4];
	Apply(m, 1.0f, 0.0f, 0.0f, 1.0f, out);
	EXPECT_NEAR(out[0], 0.0f, kEps);
	EXPECT_NEAR(out[1], 1.0f, kEps);
	EXPECT_NEAR(out[2], 0.0f, kEps);
}

TEST(Matrix, Rotation90AboutXAndY)
{
	// [rc4l] +90 about X maps +Y -> +Z; a non-unit axis is normalized first.
	float out[4];
	Apply(ComputeRotation(90.0f, 5.0f, 0.0f, 0.0f), 0.0f, 1.0f, 0.0f, 1.0f, out);
	EXPECT_NEAR(out[1], 0.0f, kEps);
	EXPECT_NEAR(out[2], 1.0f, kEps);
	// [rc4l] +90 about Y maps +Z -> +X.
	Apply(ComputeRotation(90.0f, 0.0f, 1.0f, 0.0f), 0.0f, 0.0f, 1.0f, 1.0f, out);
	EXPECT_NEAR(out[0], 1.0f, kEps);
	EXPECT_NEAR(out[2], 0.0f, kEps);
}

TEST(Matrix, OrthoMapsCornersToClipCube)
{
	Mat4 m = ComputeOrtho(0.0f, 640.0f, 0.0f, 480.0f, -1.0f, 1.0f);
	float out[4];
	// [rc4l] bottom-left -> (-1,-1), top-right -> (+1,+1)
	Apply(m, 0.0f, 0.0f, 0.0f, 1.0f, out);
	EXPECT_NEAR(out[0], -1.0f, kEps);
	EXPECT_NEAR(out[1], -1.0f, kEps);
	Apply(m, 640.0f, 480.0f, 0.0f, 1.0f, out);
	EXPECT_NEAR(out[0], 1.0f, kEps);
	EXPECT_NEAR(out[1], 1.0f, kEps);
	// [rc4l] glOrtho maps eye-z = -near -> NDC -1 and eye-z = -far -> NDC +1 (here near=-1, far=1).
	Apply(m, 0.0f, 0.0f, 1.0f, 1.0f, out); // eye-z = -near = 1
	EXPECT_NEAR(out[2], -1.0f, kEps);
	Apply(m, 0.0f, 0.0f, -1.0f, 1.0f, out); // eye-z = -far = -1
	EXPECT_NEAR(out[2], 1.0f, kEps);
}

TEST(Matrix, PerspectiveShape)
{
	Mat4 m = ComputePerspective(90.0f, 2.0f, 1.0f, 100.0f);
	// [rc4l] focal = 1/tan(45deg) = 1. m[0]=focal/aspect=0.5, m[5]=1, m[11]=-1, clip w = -z.
	EXPECT_NEAR(m.m[0], 0.5f, kEps);
	EXPECT_NEAR(m.m[5], 1.0f, kEps);
	EXPECT_NEAR(m.m[11], -1.0f, kEps);
	EXPECT_FLOAT_EQ(m.m[15], 0.0f);
	// [rc4l] A point on the near plane (z=-n) projects to clip z=-w (the near clip boundary).
	float out[4];
	Apply(m, 0.0f, 0.0f, -1.0f, 1.0f, out);
	EXPECT_NEAR(out[2] / out[3], -1.0f, 1e-4f);
	// [rc4l] A point on the far plane (z=-f) projects to clip z=+w.
	Apply(m, 0.0f, 0.0f, -100.0f, 1.0f, out);
	EXPECT_NEAR(out[2] / out[3], 1.0f, 1e-4f);
}
} // namespace
