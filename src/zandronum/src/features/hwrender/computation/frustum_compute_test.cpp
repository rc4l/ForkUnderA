// [rc4l] Tests for frustum extraction/culling (hwrender).
#include "gtest/gtest.h"
#include "features/hwrender/computation/frustum_compute.h"
#include "features/hwrender/computation/matrix_compute.h"

using namespace hwrender;

namespace
{
TEST(Frustum, PlaneDistanceSign)
{
	Plane p{1.0f, 0.0f, 0.0f, 1.0f}; // x = -1, inside is x > -1
	EXPECT_FLOAT_EQ(ComputePlaneDistance(p, 0.0f, 0.0f, 0.0f), 1.0f);
	EXPECT_FLOAT_EQ(ComputePlaneDistance(p, -2.0f, 0.0f, 0.0f), -1.0f);
}

TEST(Frustum, OrthoCubeContainsCenterRejectsOutside)
{
	// [rc4l] Ortho over [-1,1]^3 gives a frustum equal to that cube.
	Frustum f = ComputeFrustumPlanes(ComputeOrtho(-1, 1, -1, 1, -1, 1));

	// [rc4l] Center is inside.
	EXPECT_TRUE(ComputeSphereInFrustum(f, 0.0f, 0.0f, 0.0f, 0.1f));
	// [rc4l] Far outside on +X is rejected.
	EXPECT_FALSE(ComputeSphereInFrustum(f, 5.0f, 0.0f, 0.0f, 0.1f));
	// [rc4l] Outside on -Y and +Z too.
	EXPECT_FALSE(ComputeSphereInFrustum(f, 0.0f, -5.0f, 0.0f, 0.1f));
	EXPECT_FALSE(ComputeSphereInFrustum(f, 0.0f, 0.0f, 9.0f, 0.1f));
}

TEST(Frustum, SphereStraddlingBoundaryCountsAsInside)
{
	Frustum f = ComputeFrustumPlanes(ComputeOrtho(-1, 1, -1, 1, -1, 1));
	// [rc4l] Center just past +X=1 but radius reaches back inside -> conservatively inside.
	EXPECT_TRUE(ComputeSphereInFrustum(f, 1.05f, 0.0f, 0.0f, 0.1f));
	// [rc4l] Same center, tiny radius -> outside.
	EXPECT_FALSE(ComputeSphereInFrustum(f, 1.05f, 0.0f, 0.0f, 0.01f));
}

TEST(Frustum, PlanesAreNormalized)
{
	Frustum f = ComputeFrustumPlanes(ComputePerspective(90.0f, 1.0f, 1.0f, 100.0f));
	for (int i = 0; i < 6; i++)
	{
		const Plane &p = f.planes[i];
		float len = p.a * p.a + p.b * p.b + p.c * p.c;
		EXPECT_NEAR(len, 1.0f, 1e-4f) << "plane " << i;
	}
}

TEST(Frustum, DegenerateZeroMatrixDoesNotCrash)
{
	// [rc4l] A zero matrix yields zero-length normals; extraction must return them, not divide by 0.
	Mat4 zero = {};
	Frustum f = ComputeFrustumPlanes(zero);
	for (int i = 0; i < 6; i++)
	{
		EXPECT_FLOAT_EQ(f.planes[i].a, 0.0f);
		EXPECT_FLOAT_EQ(f.planes[i].b, 0.0f);
		EXPECT_FLOAT_EQ(f.planes[i].c, 0.0f);
	}
}
} // namespace
