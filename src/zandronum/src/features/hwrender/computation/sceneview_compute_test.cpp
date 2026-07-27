// [rc4l] Covers the scene view/projection construction.
#include <gtest/gtest.h>

#include <math.h>

#include "sceneview_compute.h"

namespace
{
using namespace hwrender;
// [rc4l] Column-major matrix times a column vector.
void Apply(const Mat4 &mat, float x, float y, float z, float w, float out[4])
{
	for (int row = 0; row < 4; row++)
		out[row] = mat.m[0 * 4 + row] * x + mat.m[1 * 4 + row] * y +
		           mat.m[2 * 4 + row] * z + mat.m[3 * 4 + row] * w;
}
}

using namespace hwrender;

TEST(SceneView, FovYMatchesTheLegacyDerivation)
{
	// [rc4l] With fovratio 1 the vertical and horizontal FOV coincide.
	EXPECT_NEAR(90.0f, ComputeSceneFovY(90.0f, 1.0f), 1e-3f);

	// [rc4l] A wider ratio narrows the vertical FOV.
	EXPECT_LT(ComputeSceneFovY(90.0f, 1.6f), 90.0f);

	// [rc4l] Degenerate ratio falls back to the horizontal FOV rather than dividing by zero.
	EXPECT_EQ(75.0f, ComputeSceneFovY(75.0f, 0.0f));
}

TEST(SceneView, ProjectionPutsTheNearPlaneAtMinusOne)
{
	const Mat4 p = ComputeSceneProjection(90.0f, 1.0f, 1.0f);
	float out[4];
	// [rc4l] A point on the near plane (5 units down -Z) maps to ndc z = -1.
	Apply(p, 0.0f, 0.0f, -5.0f, 1.0f, out);
	ASSERT_NE(0.0f, out[3]);
	EXPECT_NEAR(-1.0f, out[2] / out[3], 1e-3f);
}

TEST(SceneView, ViewPlacesTheCameraAtTheOrigin)
{
	// [rc4l] With no rotation, the camera position itself must map to the origin.
	const Mat4 v = ComputeSceneView(0.0f, 0.0f, 0.0f, 100.0f, 200.0f, 40.0f, false, false);
	float out[4];
	// [rc4l] Doom (x, y, z) enters GL as (x, z, y).
	Apply(v, 100.0f, 40.0f, 200.0f, 1.0f, out);
	EXPECT_NEAR(0.0f, out[0], 1e-3f);
	EXPECT_NEAR(0.0f, out[1], 1e-3f);
	EXPECT_NEAR(0.0f, out[2], 1e-3f);
}

TEST(SceneView, MirrorFlipsTheHorizontalAxis)
{
	const Mat4 plain = ComputeSceneView(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false);
	const Mat4 mirrored = ComputeSceneView(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, false);
	float a[4], b[4];
	Apply(plain, 1.0f, 0.0f, 0.0f, 1.0f, a);
	Apply(mirrored, 1.0f, 0.0f, 0.0f, 1.0f, b);
	// [rc4l] The unmirrored view already negates X; mirroring cancels that.
	EXPECT_NEAR(-a[0], b[0], 1e-3f);
}

TEST(SceneView, PlaneMirrorFlipsTheVerticalAxis)
{
	const Mat4 plain = ComputeSceneView(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false);
	const Mat4 flipped = ComputeSceneView(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, true);
	float a[4], b[4];
	Apply(plain, 0.0f, 1.0f, 0.0f, 1.0f, a);
	Apply(flipped, 0.0f, 1.0f, 0.0f, 1.0f, b);
	EXPECT_NEAR(-a[1], b[1], 1e-3f);
}

TEST(SceneView, YawRotatesAroundTheVerticalAxis)
{
	// [rc4l] A 90 degree yaw must move a point that lay ahead onto the horizontal axis.
	const Mat4 v = ComputeSceneView(0.0f, 0.0f, 90.0f, 0.0f, 0.0f, 0.0f, false, false);
	float out[4];
	Apply(v, 0.0f, 0.0f, 1.0f, 1.0f, out);
	EXPECT_NEAR(0.0f, out[2], 1e-3f);
	EXPECT_NEAR(1.0f, fabsf(out[0]), 1e-3f);
}

TEST(SceneView, PitchRotatesAroundTheHorizontalAxis)
{
	const Mat4 v = ComputeSceneView(0.0f, 90.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false);
	float out[4];
	Apply(v, 0.0f, 1.0f, 0.0f, 1.0f, out);
	EXPECT_NEAR(0.0f, out[1], 1e-3f);
}

TEST(SceneView, RollRotatesAroundTheViewAxis)
{
	const Mat4 v = ComputeSceneView(90.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false);
	float out[4];
	Apply(v, 0.0f, 0.0f, 1.0f, 1.0f, out);
	// [rc4l] Roll leaves the view axis itself untouched.
	EXPECT_NEAR(1.0f, fabsf(out[2]), 1e-3f);
}
