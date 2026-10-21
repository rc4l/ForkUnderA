// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/sceneconstants_compute.h"
#include "features/hwrender/computation/lightcluster_compute.h"
#include "features/hwrender/computation/warp_compute.h"

namespace zx { namespace hwrender {

void ComputeFixedColormapConstants(const zx::viewsetup::FixedColormapShade &shade, float out[8])
{
	const bool drawn = shade.mode == zx::viewsetup::kShadeRamp || shade.mode == zx::viewsetup::kShadeTint;
	for (int i = 0; i < 3; i++) { out[i] = drawn ? shade.start[i] : 0.f; out[4 + i] = drawn ? shade.range[i] : 0.f; }
	out[3] = drawn ? (float)shade.mode : 0.f;
	out[7] = 0.f;
}

void ComputeSceneConstants(const SceneConstantInputs &in, float *out)
{
	for (int i = 0; i < 16; i++) out[i] = in.mvp[i];
	out[16] = in.camX; out[17] = in.camZ; out[18] = in.camY; out[19] = in.lightMode;
	out[20] = in.lightCount; out[21] = in.skyAngle;
	// [rc4l] The light grid's depth range, zero when the grid is off so the shader loops over every light.
	out[22] = in.clusters ? kClusterZNear : 0.f;
	out[23] = in.clusters ? kClusterZFar : 0.f;
	for (int i = 0; i < 4; i++) out[24 + i] = (in.clip != nullptr) ? in.clip[i] : 0.f;
	out[28] = in.targetW; out[29] = in.targetH; out[30] = in.skyXScale; out[31] = in.skyVScale;
	out[32] = in.skyCapR / 255.f; out[33] = in.skyCapG / 255.f; out[34] = in.skyCapB / 255.f;
	out[35] = in.materialSlots;
	out[36] = ComputeWarpTimer(in.frameMS, 1.f);
	out[37] = (float)(in.walkSlot + 1);
	out[38] = in.playerPass ? (float)in.skyHoles : 0.f;
	out[39] = in.spriteAlphaCut;
	ComputeFixedColormapConstants(in.fixedColormap, out + 40);
	out[48] = in.lightsCheckSide ? 1.f : 0.f;
	out[49] = (float)in.walkSlots;
	out[50] = out[51] = 0.f;
}

}} // namespace zx::hwrender
