// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The scene constant block every Vulkan pass binds, packed in one place; its GLSL twin is FUA_CONSTANTS_GLSL in dgshared.h.

#ifndef ZX_SCENECONSTANTS_COMPUTE_H
#define ZX_SCENECONSTANTS_COMPUTE_H

#include "features/viewsetup/computation/viewsetup_compute.h"   // [rc4l] FixedColormapShade

namespace zx { namespace hwrender {

// [rc4l] mat4 uMVP, then uCameraPos, uLightParams, uClipPlane, uScreen, uSkyColor, uFrame, uFixedColormap, uFixedRange and uOptions, a vec4 each.
enum { kSceneConstantFloats = 52 };

// [rc4l] uFixedColormap (the start colour, the mode in w) and uFixedRange (the ramp, w 0) from a view's shade; only a ramp or a tint is ever a view's, so anything else reads as none.
void ComputeFixedColormapConstants(const zx::viewsetup::FixedColormapShade &shade, float out[8]);

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
	int walkSlot;                     // [rc4l] the walk slot whose drawing alone this pass draws (ComputePassWalkSlot), else -1; uFrame.y carries it plus one
	bool playerPass;                  // [rc4l] the player's own pass, the main view outside any line mirror, the only one whose walk the holes and walk bits are
	int skyHoles;                     // [rc4l] the player's sky holes this frame, which uFrame.z carries to the player's pass alone
	float spriteAlphaCut;             // [rc4l] the texel alpha at or below which an alpha-tested sprite is dropped, GL's uAlphaThreshold for r_mask_sprite_threshold; uFrame.w
	zx::viewsetup::FixedColormapShade fixedColormap;   // [rc4l] what the view's fixed colormap does to every texel, uFixedColormap and uFixedRange
	bool lightsCheckSide;             // [rc4l] r_lights_checkside: a light behind a surface's plane does not reach it; uOptions.x
	int walkSlots;                    // [rc4l] the walk slots each id has this level (ComputeWalkSlots), the stride the walk bits are read by; uOptions.y
};

// [rc4l] Fill `out` (kSceneConstantFloats floats) from `in`.
void ComputeSceneConstants(const SceneConstantInputs &in, float *out);

}} // namespace zx::hwrender

#endif // ZX_SCENECONSTANTS_COMPUTE_H
