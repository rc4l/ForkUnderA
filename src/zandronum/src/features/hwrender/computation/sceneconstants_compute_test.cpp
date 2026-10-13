// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/sceneconstants_compute.h"
#include "features/hwrender/computation/lightcluster_compute.h"

using namespace zx::hwrender;

namespace {

// [rc4l] Inputs with every field distinct, so a value written to the wrong index shows up as the wrong number.
SceneConstantInputs Inputs(const float *mvp, const float *clip)
{
	SceneConstantInputs in;
	in.mvp = mvp;
	in.camX = 10.f; in.camY = 20.f; in.camZ = 30.f;
	in.lightMode = 2.f; in.lightCount = 7.f; in.skyAngle = 0.5f;
	in.clusters = true;
	in.clip = clip;
	in.targetW = 1280.f; in.targetH = 720.f; in.skyXScale = 1.5f; in.skyVScale = 0.75f;
	in.skyCapR = 255; in.skyCapG = 0; in.skyCapB = 51;
	in.materialSlots = 4096.f;
	in.frameMS = 2500u;
	in.walkSlot = 1;
	in.playerPass = true;
	in.skyHoles = 3;
	in.spriteAlphaCut = 0.499f;
	in.fixedColormap = { zx::viewsetup::kShadeRamp, { 1.f, 0.5f, 0.25f }, { -1.f, -0.5f, 0.125f } };
	return in;
}

} // namespace

// [rc4l] The block's layout is FUA_CONSTANTS_GLSL's, index for index: three passes wrote it by hand before this.
TEST(SceneConstants, EveryFieldLandsWhereTheShaderReadsIt)
{
	float mvp[16];
	for (int i = 0; i < 16; i++) mvp[i] = (float)(100 + i);
	const float clip[4] = { 0.f, 1.f, 0.f, -64.f };
	float out[kSceneConstantFloats];
	ComputeSceneConstants(Inputs(mvp, clip), out);

	for (int i = 0; i < 16; i++) EXPECT_EQ(100.f + i, out[i]);
	// [rc4l] The camera goes in as (x, z, y), the axis swap the mesh vertices get.
	EXPECT_EQ(10.f, out[16]); EXPECT_EQ(30.f, out[17]); EXPECT_EQ(20.f, out[18]); EXPECT_EQ(2.f, out[19]);
	EXPECT_EQ(7.f, out[20]); EXPECT_EQ(0.5f, out[21]);
	EXPECT_EQ(kClusterZNear, out[22]); EXPECT_EQ(kClusterZFar, out[23]);
	EXPECT_EQ(-64.f, out[27]); EXPECT_EQ(1.f, out[25]);
	EXPECT_EQ(1280.f, out[28]); EXPECT_EQ(720.f, out[29]); EXPECT_EQ(1.5f, out[30]); EXPECT_EQ(0.75f, out[31]);
	EXPECT_FLOAT_EQ(1.f, out[32]); EXPECT_FLOAT_EQ(0.f, out[33]); EXPECT_FLOAT_EQ(0.2f, out[34]);
	EXPECT_EQ(4096.f, out[35]);
	EXPECT_FLOAT_EQ(2.5f, out[36]);
	EXPECT_EQ(2.f, out[37]); EXPECT_EQ(3.f, out[38]); EXPECT_EQ(0.499f, out[39]);
	EXPECT_EQ(1.f, out[40]); EXPECT_EQ(0.5f, out[41]); EXPECT_EQ(0.25f, out[42]); EXPECT_EQ(1.f, out[43]);
	EXPECT_EQ(-1.f, out[44]); EXPECT_EQ(-0.5f, out[45]); EXPECT_EQ(0.125f, out[46]); EXPECT_EQ(0.f, out[47]);
}

// [rc4l] A tint keeps its colour and mode 2; no colormap, or a sprite's fog layer, which is never a view's, reads as all zeros.
TEST(SceneConstants, OnlyARampOrATintReachesTheShader)
{
	float out[8];
	ComputeFixedColormapConstants({ zx::viewsetup::kShadeTint, { 0.375f, 1.f, 0.375f }, { 0.f, 0.f, 0.f } }, out);
	EXPECT_EQ(0.375f, out[0]); EXPECT_EQ(1.f, out[1]); EXPECT_EQ(2.f, out[3]);
	for (int mode : { zx::viewsetup::kShadeNone, zx::viewsetup::kShadeFogLayer })
	{
		ComputeFixedColormapConstants({ mode, { 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f } }, out);
		for (int i = 0; i < 8; i++) EXPECT_EQ(0.f, out[i]);
	}
}

// [rc4l] Every pass but a stacked view's reads zero, which the scene shader takes as "cull nothing".
TEST(SceneConstants, NoStackedViewReadsZero)
{
	float mvp[16] = {};
	SceneConstantInputs in = Inputs(mvp, nullptr);
	in.walkSlot = -1;
	float out[kSceneConstantFloats];
	ComputeSceneConstants(in, out);
	EXPECT_EQ(0.f, out[37]);
}

// [rc4l] WDI18's forest: a mirror's, a stacked view's or a camera's pass tests no sky hole, since the walk bits are the player's walk's.
TEST(SceneConstants, OnlyThePlayersPassTestsSkyHoles)
{
	float mvp[16] = {};
	SceneConstantInputs in = Inputs(mvp, nullptr);
	in.playerPass = false;
	float out[kSceneConstantFloats];
	ComputeSceneConstants(in, out);
	EXPECT_EQ(0.f, out[38]);
}

// [rc4l] A zero cluster range is how the shader is told the grid is off, and a missing clip plane is all zeros (w 0 disables it).
TEST(SceneConstants, GridOffAndNoClipReadAsZeros)
{
	float mvp[16] = {};
	SceneConstantInputs in = Inputs(mvp, nullptr);
	in.clusters = false;
	float out[kSceneConstantFloats];
	ComputeSceneConstants(in, out);
	EXPECT_EQ(0.f, out[22]);
	EXPECT_EQ(0.f, out[23]);
	for (int i = 24; i < 28; i++) EXPECT_EQ(0.f, out[i]);
}
