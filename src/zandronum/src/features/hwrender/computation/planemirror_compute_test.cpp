// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/planemirror_compute.h"

using namespace zx::hwrender;

// [rc4l] wdiexsec: its reflective floors showed what lies under them on Vulkan, which drew them translucent and reflected nothing.
TEST(PlaneMirror, AFloorReflectsAnEyeAboveItAndACeilingOneBelow)
{
	EXPECT_TRUE(ComputePlaneMirrorFaces(false, -32.f, 18.f));
	EXPECT_TRUE(ComputePlaneMirrorFaces(false, -32.f, -32.f));
	EXPECT_FALSE(ComputePlaneMirrorFaces(false, -32.f, -40.f));
	EXPECT_TRUE(ComputePlaneMirrorFaces(true, 128.f, 41.f));
	EXPECT_FALSE(ComputePlaneMirrorFaces(true, 128.f, 130.f));
}

TEST(PlaneMirror, TheReflectedEyeIsAsFarBehindThePlaneAsTheEyeIsInFront)
{
	EXPECT_FLOAT_EQ(-82.f, ComputePlaneMirrorEyeZ(-32.f, 18.f));
	EXPECT_FLOAT_EQ(215.f, ComputePlaneMirrorEyeZ(128.f, 41.f));
}

// [rc4l] The clip keeps what is above a floor mirror and below a ceiling one, in mesh space where y is up; a plane at zero must not read as "no clip".
TEST(PlaneMirror, TheClipKeepsOnlyWhatTheMirrorFaces)
{
	float c[4];
	ComputePlaneMirrorClip(false, -32.f, c);
	EXPECT_GT(c[1] * 10.f + c[3], 0.f);
	EXPECT_LT(c[1] * -50.f + c[3], 0.f);
	ComputePlaneMirrorClip(true, 128.f, c);
	EXPECT_GT(c[1] * 100.f + c[3], 0.f);
	EXPECT_LT(c[1] * 200.f + c[3], 0.f);
	EXPECT_FLOAT_EQ(0.f, c[0]);
	EXPECT_FLOAT_EQ(0.f, c[2]);
	ComputePlaneMirrorClip(false, 0.f, c);
	EXPECT_NE(0.f, c[3]);
	ComputePlaneMirrorClip(true, 0.f, c);
	EXPECT_NE(0.f, c[3]);
}

TEST(PlaneMirror, OnePlaneIsOneSlotAndTheSlotsAreCapped)
{
	MirrorPlane slots[2];
	int used = 0;
	EXPECT_EQ(0, ComputePlaneMirrorSlot(slots, used, 2, MirrorPlane{ false, -32.f }));
	EXPECT_EQ(0, ComputePlaneMirrorSlot(slots, used, 2, MirrorPlane{ false, -32.f }));
	EXPECT_EQ(1, ComputePlaneMirrorSlot(slots, used, 2, MirrorPlane{ true, -32.f }));
	EXPECT_EQ(-1, ComputePlaneMirrorSlot(slots, used, 2, MirrorPlane{ false, 0.f }));
	EXPECT_EQ(2, used);
}

namespace {

// [rc4l] A plain perspective along -z (column-major, Vulkan depth 0..1), enough to put boxes in and out of view.
void Perspective(float *m)
{
	for (int i = 0; i < 16; i++) m[i] = 0.f;
	const float zn = 1.f, zf = 100.f;
	m[0] = 1.f; m[5] = 1.f; m[10] = zf / (zn - zf); m[11] = -1.f; m[14] = zf * zn / (zn - zf);
}

} // namespace

TEST(PlaneMirror, ABoxOffScreenIsSkippedAndOneOnScreenIsNot)
{
	float m[16];
	Perspective(m);
	const float inMin[3] = { -1.f, -1.f, -10.f }, inMax[3] = { 1.f, 1.f, -5.f };
	EXPECT_TRUE(ComputeBoxMayBeVisible(m, inMin, inMax));
	const float behindMin[3] = { -1.f, -1.f, 5.f }, behindMax[3] = { 1.f, 1.f, 10.f };
	EXPECT_FALSE(ComputeBoxMayBeVisible(m, behindMin, behindMax));
	const float leftMin[3] = { -50.f, -1.f, -10.f }, leftMax[3] = { -40.f, 1.f, -5.f };
	EXPECT_FALSE(ComputeBoxMayBeVisible(m, leftMin, leftMax));
	const float rightMin[3] = { 40.f, -1.f, -10.f }, rightMax[3] = { 50.f, 1.f, -5.f };
	EXPECT_FALSE(ComputeBoxMayBeVisible(m, rightMin, rightMax));
	const float upMin[3] = { -1.f, 40.f, -10.f }, upMax[3] = { 1.f, 50.f, -5.f };
	EXPECT_FALSE(ComputeBoxMayBeVisible(m, upMin, upMax));
	const float downMin[3] = { -1.f, -50.f, -10.f }, downMax[3] = { 1.f, -40.f, -5.f };
	EXPECT_FALSE(ComputeBoxMayBeVisible(m, downMin, downMax));
	const float farMin[3] = { -1.f, -1.f, -500.f }, farMax[3] = { 1.f, 1.f, -400.f };
	EXPECT_FALSE(ComputeBoxMayBeVisible(m, farMin, farMax));
	// [rc4l] A floor the eye stands on spans the screen edge and behind the eye at once, and must stay.
	const float spanMin[3] = { -60.f, -2.f, -60.f }, spanMax[3] = { 60.f, -2.f, 60.f };
	EXPECT_TRUE(ComputeBoxMayBeVisible(m, spanMin, spanMax));
}
