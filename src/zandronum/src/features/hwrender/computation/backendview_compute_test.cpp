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

// [rc4l] The view alone, which the mirror sheen reflects in: the eye at the origin, what it faces down -z, and heights stretched.
TEST(BackendViewCompute, TheViewPutsTheEyeAtTheOriginFacingDownZ)
{
	BackendCamera c = Corner(0.f, 1.2f);
	c.x = 100.f; c.y = 200.f; c.z = 50.f;
	c.yaw = 90.f * kDeg;   // [rc4l] facing north
	float v[16];
	ComputeBackendView(c, v);
	// [rc4l] Mesh space is (x, height, y).
	auto eye = [&](float x, float h, float y, int r) { return v[r] * x + v[4 + r] * h + v[8 + r] * y + v[12 + r]; };
	for (int r = 0; r < 3; r++) EXPECT_NEAR(0.f, eye(100.f, 50.f, 200.f, r), 1e-3f) << r;
	EXPECT_NEAR(-10.f, eye(100.f, 50.f, 210.f, 2), 1e-3f);
	EXPECT_NEAR(12.f, eye(100.f, 60.f, 200.f, 1), 1e-3f);
	EXPECT_FLOAT_EQ(1.f, v[15]);
}

namespace {

TEST(ComputeViewWindowViewport, AFullScreenViewIsTheWholeScreen)
{
	const ScreenRect r = ComputeViewWindowViewport(640, 11, 0, 0, 1024, 640);
	EXPECT_EQ(r.x, 0); EXPECT_EQ(r.y, 0); EXPECT_EQ(r.w, 1024); EXPECT_EQ(r.h, 640);
}

TEST(ComputeViewWindowViewport, AStatusBarRaisesTheWholeScreenByHalfItsHeight)
{
	// [rc4l] screenblocks 10 on 1024x640: the bar takes 100 rows, and GL keeps the screen's height so the view is cropped, not squashed.
	const ScreenRect r = ComputeViewWindowViewport(640, 10, 0, 0, 1024, 540);
	EXPECT_EQ(r.x, 0); EXPECT_EQ(r.y, -50); EXPECT_EQ(r.w, 1024); EXPECT_EQ(r.h, 640);
}

TEST(ComputeViewWindowViewport, ASmallerViewIsScreenblocksTenthsCentredOnItsWindow)
{
	// [rc4l] screenblocks 8: 640 * 8 / 10 = 512 rows, already a multiple of 8, around a 432-row window at row 40.
	const ScreenRect r = ComputeViewWindowViewport(640, 8, 103, 40, 816, 432);
	EXPECT_EQ(r.x, 103); EXPECT_EQ(r.y, 0); EXPECT_EQ(r.w, 816); EXPECT_EQ(r.h, 512);
	// [rc4l] 7 * 600 / 10 = 420 rounds down to 416.
	EXPECT_EQ(ComputeViewWindowViewport(600, 7, 0, 0, 560, 416).h, 416);
}

TEST(ComputeNdcWindow, TheWholeScreenChangesNothing)
{
	const NdcWindow w = ComputeNdcWindow(ComputeViewWindowViewport(640, 11, 0, 0, 1024, 640), 1024, 640);
	EXPECT_FLOAT_EQ(w.sx, 1.f); EXPECT_FLOAT_EQ(w.sy, 1.f); EXPECT_FLOAT_EQ(w.ox, 0.f); EXPECT_FLOAT_EQ(w.oy, 0.f);
}

TEST(ComputeApplyNdcWindow, ThePointsLandWhereTheViewportWouldPutThem)
{
	// [rc4l] A raised status-bar view: the centre of the view goes to row 270 of 640, the middle of the 540 rows above the bar.
	const ScreenRect rect = { 0, -50, 1024, 640 };
	const NdcWindow w = ComputeNdcWindow(rect, 1024, 640);
	float m[16];
	ComputeBackendMVP(Corner(0.f, 1.2f), m);
	float before[16];
	for (int i = 0; i < 16; i++) before[i] = m[i];
	ComputeApplyNdcWindow(w, m);
	const float p[4] = { 100.f, 60.f, 50.f, 1.f };
	float a[4], b[4];
	for (int r = 0; r < 4; r++)
	{
		a[r] = before[r] * p[0] + before[4 + r] * p[1] + before[8 + r] * p[2] + before[12 + r] * p[3];
		b[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] * p[3];
	}
	// [rc4l] The viewport's own mapping from the untouched matrix, against the window's from the touched one, as top-down pixels.
	const float wantX = rect.x + (a[0] / a[3] + 1.f) * 0.5f * rect.w;
	const float wantY = rect.y + (1.f - a[1] / a[3]) * 0.5f * rect.h;
	EXPECT_NEAR((b[0] / b[3] + 1.f) * 0.5f * 1024.f, wantX, 1e-3f);
	EXPECT_NEAR((1.f - b[1] / b[3]) * 0.5f * 640.f, wantY, 1e-3f);
	EXPECT_FLOAT_EQ(b[2], a[2]);
	EXPECT_FLOAT_EQ(b[3], a[3]);
	EXPECT_NEAR(w.oy, 50.f / 320.f, 1e-6f);
}

} // namespace

TEST(ComputeMatrix4, MultiplyComposesAndTheInverseUndoes)
{
	using namespace zx::hwrender;
	// [rc4l] A translation by (1, 2, 3) then a scale by 2, column-major.
	const float t[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 1,2,3,1 };
	const float s[16] = { 2,0,0,0, 0,2,0,0, 0,0,2,0, 0,0,0,1 };
	float ts[16], inv[16], back[16];
	ComputeMultiplyMatrix4(t, s, ts);
	EXPECT_FLOAT_EQ(2.f, ts[0]);
	EXPECT_FLOAT_EQ(3.f, ts[14]);
	ASSERT_TRUE(ComputeInvertMatrix4(ts, inv));
	ComputeMultiplyMatrix4(ts, inv, back);
	for (int i = 0; i < 16; i++) EXPECT_NEAR(i % 5 == 0 ? 1.f : 0.f, back[i], 1e-5f) << i;
	const float flat[16] = { 0 };
	EXPECT_FALSE(ComputeInvertMatrix4(flat, inv));
}

TEST(ComputeEyeToMesh, TheEyesOriginIsTheCameraAndTheViewThenUndoesIt)
{
	using namespace zx::hwrender;
	BackendCamera c;
	c.x = 100.f; c.y = 200.f; c.z = 40.f;
	c.yaw = 0.7f; c.pitch = 0.2f; c.pixelStretch = 1.2f;
	c.fovY = 1.2f; c.aspect = 1.6f;
	const float eye[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
	float m[16], v[16], round[16];
	ASSERT_TRUE(ComputeEyeToMesh(c, eye, m));
	// [rc4l] Mesh space is (x, up, y).
	EXPECT_NEAR(100.f, m[12], 1e-3f);
	EXPECT_NEAR(40.f, m[13], 1e-3f);
	EXPECT_NEAR(200.f, m[14], 1e-3f);
	ComputeBackendView(c, v);
	ComputeMultiplyMatrix4(v, m, round);
	for (int i = 0; i < 16; i++) EXPECT_NEAR(eye[i], round[i], 1e-4f) << i;
	c.pixelStretch = 0.f;
	c.yaw = 0.f; c.pitch = 0.f;
	const float none[16] = { 0 };
	EXPECT_TRUE(ComputeEyeToMesh(c, none, m));
}
