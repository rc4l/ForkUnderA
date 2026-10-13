// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The world pass's pipelines, one per fragment stage and fixed-function state the level, its sprites and its decals draw with, and which one a draw takes.

#ifndef ZX_WORLDPIPELINES_COMPUTE_H
#define ZX_WORLDPIPELINES_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] Every world pipeline, in the order they are built and bound.
enum WorldPipeline
{
	kWpOpaque, kWpMasked, kWpTrans, kWpAdd,          // [rc4l] the level's surfaces, culled as fua_dg_cull says
	kWpMaskedNoCull, kWpTransNoCull, kWpAddNoCull,   // [rc4l] sprites and models, never culled, since a billboard's winding is not promised
	kWpMaskedDecal, kWpTransDecal, kWpAddDecal,      // [rc4l] decals, never culled and depth-biased onto their wall as GL's glPolygonOffset(-1, -128)
	kWpTransRedAlpha, kWpAddRedAlpha,                // [rc4l] shaded decals, whose texture's red channel is their coverage
	kWpMaskedGBuffer,                                // [rc4l] the masked world writing its normals for the decal pass
	kWpRevSubNoCull, kWpSubNoCull,                   // [rc4l] sprites in the subtract styles, the screen less the sprite and the sprite less the screen
	kWorldPipelines
};

// [rc4l] The fragment stages they are built from (dgscene_glsl.h).
enum { kWpStageOpaque, kWpStageMasked, kWpStageTrans, kWpStageRedAlpha, kWpStageGBuffer };

// [rc4l] Their blends: none, which writes depth, source alpha over, source alpha added, and source alpha taken from the screen or the screen from it.
enum { kWpBlendNone, kWpBlendAlpha, kWpBlendAdd, kWpBlendRevSub, kWpBlendSub };

struct WorldPipelineDesc
{
	const char *name;
	int  stage;       // [rc4l] a kWpStage* value
	int  blend;       // [rc4l] a kWpBlend* value; anything blended writes no depth
	bool level;       // [rc4l] draws the level's own surfaces, which take fua_dg_cull's culling; the others draw the dynamic stream
	bool depthBias;   // [rc4l] a decal's
};

// [rc4l] What `pipeline` (a WorldPipeline) is built with.
WorldPipelineDesc ComputeWorldPipelineDesc(int pipeline);

// [rc4l] A level batch's pipeline from its blend mode (MeshPiece::blendMode): 0 masked, 2 additive, anything else translucent.
int ComputeBatchPipeline(int blendMode);

// [rc4l] A sprite's, model's or decal's pipeline in the translucent pass, from its blend mode (2 additive, 4 and 5 the subtract styles), whether its texture is a red-channel mask and whether it is a decal; a decal in a subtract style draws translucent.
int ComputeDynamicPipeline(int blendMode, bool redAlpha, bool depthBias);

}} // namespace zx::hwrender

#endif // ZX_WORLDPIPELINES_COMPUTE_H
