// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include <math.h>
#include "features/hwrender/computation/backendview_compute.h"

using namespace zx::hwrender;

namespace {

const float kDeg = 3.14159265f / 180.f;

// [rc4l] The stackportal template's upper room: the eye in its north-east corner, facing the south-west corner, as the backend frames a 1024x640 window.
BackendCamera Corner(float pitchDegrees, float stretch)
{
	BackendCamera c;
	c.x = 448.f; c.y = 448.f; c.z = 169.f;
	c.yaw = 225.f * kDeg;
	c.pitch = pitchDegrees * kDeg;
	c.pixelStretch = stretch;
	c.fovY = 2.f * atanf(0.75f);   // [rc4l] GL's, from a 90 degree field of view and the 4:3 fovratio
	c.aspect = 1.6f;
	return c;
}

// [rc4l] A map point (x, y, height) through the matrix, as normalised device x, y (up) and depth.
void Project(const BackendCamera &c, float x, float y, float h, float ndc[3])
{
	float m[16];
	ComputeBackendMVP(c, m);
	const float p[4] = { x, h, y, 1.f };
	float clip[4];
	for (int r = 0; r < 4; r++) clip[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] * p[3];
	for (int k = 0; k < 3; k++) ndc[k] = clip[k] / clip[3];
}

// [rc4l] GL's row for a point on a 640-row frame, as normalised device y.
float RowToNdcY(float row) { return (320.f - row) / 320.f; }

} // namespace

// [rc4l] Rows read off GL's frames of the room's south-west corner, against which the backend put the wall top 39 rows low looking up 40 degrees.
TEST(BackendViewCompute, TheCornerLandsOnGLsRowLevelAndLookingUp)
{
	float ndc[3];
	Project(Corner(0.f, 1.2f), 0.f, 0.f, 128.f, ndc);     // [rc4l] the foot of the corner, level
	EXPECT_NEAR(RowToNdcY(353.f), ndc[1], 0.005f);
	Project(Corner(-20.f, 1.2f), 0.f, 0.f, 640.f, ndc);   // [rc4l] the top of the corner, looking up 20
	EXPECT_NEAR(RowToNdcY(180.f), ndc[1], 0.005f);
	Project(Corner(-40.f, 1.2f), 0.f, 0.f, 640.f, ndc);   // [rc4l] and looking up 40
	EXPECT_NEAR(RowToNdcY(345.f), ndc[1], 0.005f);
	EXPECT_NEAR(0.f, ndc[0], 1e-4f);                      // [rc4l] dead ahead
}

// [rc4l] Whatever the stretch, the middle of the screen is where the playsim aims, at its own unstretched pitch.
TEST(BackendViewCompute, TheCrosshairIsOnThePlaysimsAim)
{
	for (float stretch : { 1.f, 1.2f })
	{
		const BackendCamera c = Corner(-40.f, stretch);
		const float d = 300.f;
		float ndc[3];
		Project(c, c.x + d * cosf(c.yaw), c.y + d * sinf(c.yaw), c.z + d * tanf(40.f * kDeg), ndc);
		EXPECT_NEAR(0.f, ndc[0], 1e-4f);
		EXPECT_NEAR(0.f, ndc[1], 1e-4f);
	}
}

TEST(BackendViewCompute, NoStretchIsASquarePixelCamera)
{
	float a[3], b[3];
	Project(Corner(-20.f, 0.f), 0.f, 0.f, 640.f, a);   // [rc4l] an unset stretch reads as none
	Project(Corner(-20.f, 1.f), 0.f, 0.f, 640.f, b);
	EXPECT_FLOAT_EQ(b[1], a[1]);
	const float e = atanf((640.f - 169.f) / sqrtf(2.f * 448.f * 448.f));
	EXPECT_NEAR(tanf(e - 20.f * kDeg) / 0.75f, b[1], 1e-4f);
}

TEST(BackendViewCompute, RightIsRightAndDepthRunsZeroToOne)
{
	BackendCamera c = Corner(0.f, 1.2f);
	c.x = 0.f; c.y = 0.f; c.z = 0.f;
	c.yaw = 90.f * kDeg;   // [rc4l] facing north, so east is on the right
	float ndc[3];
	Project(c, 50.f, 100.f, 0.f, ndc);
	EXPECT_GT(ndc[0], 0.f);
	Project(c, 0.f, 5.f, 0.f, ndc);        // [rc4l] on the near plane
	EXPECT_NEAR(0.f, ndc[2], 1e-4f);
	Project(c, 0.f, 65536.f, 0.f, ndc);    // [rc4l] on the far plane
	EXPECT_NEAR(1.f, ndc[2], 1e-3f);
}
