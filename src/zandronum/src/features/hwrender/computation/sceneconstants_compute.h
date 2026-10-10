// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The scene constant block every Vulkan pass binds, packed in one place; its GLSL twin is FUA_CONSTANTS_GLSL in dgshared.h.

#ifndef ZX_SCENECONSTANTS_COMPUTE_H
#define ZX_SCENECONSTANTS_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] mat4 uMVP, then uCameraPos, uLightParams, uClipPlane, uScreen, uSkyColor and uFrame, a vec4 each.
enum { kSceneConstantFloats = 40 };

// [rc4l] What a pass knows about its frame; everything else in the block follows from these.
struct SceneConstantInputs
{
	const float *mvp;                 // [rc4l] 16 floats, column-major
	float camX, camY, camZ;           // [rc4l] map units; written as (x, z, y), the axis swap the mesh vertices get
	float lightMode, lightCount;      // [rc4l] fua_dg_lightmode and the live dynamic lights
	float skyAngle;                   // [rc4l] RenderDome's -180 degrees plus this frame's scroll
	bool  clusters;                   // [rc4l] whether the light grid was built this frame
	const float *clip;                // [rc4l] 4 floats, or null; w 0 disables the mirror clip plane
	float targetW, targetH;           // [rc4l] the target being drawn into, in pixels
	float skyXScale, skyVScale;
	unsigned char skyCapR, skyCapG, skyCapB;   // [rc4l] what a reflected ray that hits nothing sees
	float materialSlots;              // [rc4l] live bindless slots, or 0 to sample the bound texture
	unsigned int frameMS;             // [rc4l] the clock GL's gl_frameMS reads, for the warp timer
};

// [rc4l] Fill `out` (kSceneConstantFloats floats) from `in`.
void ComputeSceneConstants(const SceneConstantInputs &in, float *out);

}} // namespace zx::hwrender

#endif // ZX_SCENECONSTANTS_COMPUTE_H
