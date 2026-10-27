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
	kWpFuzzNoCull, kWpFuzzDecal,                     // [rc4l] shadows and fuzzy decals, the screen multiplied by the source
	kWpTransHud, kWpAddHud, kWpFuzzHud, kWpRevSubHud, kWpSubHud,   // [rc4l] a blended HUD model, writing depth as GL's HUD step does so it hides its own far side
	kWorldPipelines
};

// [rc4l] The fragment stages they are built from (dgscene_glsl.h).
enum { kWpStageOpaque, kWpStageMasked, kWpStageTrans, kWpStageRedAlpha, kWpStageGBuffer };

// [rc4l] Their blends: none, which writes depth, source alpha over, source alpha added, source alpha taken from the screen or the screen from it, and GL's DST_COLOR / ONE_MINUS_SRC_ALPHA fuzz fallback.
enum { kWpBlendNone, kWpBlendAlpha, kWpBlendAdd, kWpBlendRevSub, kWpBlendSub, kWpBlendFuzz };

struct WorldPipelineDesc
{
	const char *name;
	int  stage;       // [rc4l] a kWpStage* value
	int  blend;       // [rc4l] a kWpBlend* value; anything blended writes no depth unless depthWrite says so
	bool level;       // [rc4l] draws the level's own surfaces, which take fua_dg_cull's culling; the others draw the dynamic stream
	bool depthBias;   // [rc4l] a decal's
	bool depthWrite;  // [rc4l] blended and still writing depth, a HUD model's
};

// [rc4l] What `pipeline` (a WorldPipeline) is built with.
WorldPipelineDesc ComputeWorldPipelineDesc(int pipeline);

// [Doruk-Aksoy] Whether `pipeline` draws a GLDEFS shader's pieces with that shader's own program: every one but the red-alpha decals', whose stage never samples through fuaSurfaceTexel.
bool ComputeWorldPipelineTakesUserShader(int pipeline);

// [rc4l] A level batch's pipeline from its blend mode (MeshPiece::blendMode): 0 masked, 2 additive, anything else translucent.
int ComputeBatchPipeline(int blendMode);

// [rc4l] A sprite's, model's or decal's pipeline in the translucent pass, from its blend mode (2 additive, 3 fuzz, 4 and 5 the subtract styles), whether its texture is a red-channel mask, whether it is a decal and whether it is a HUD model's; DECALDEF has no subtract style, so a decal never asks for one.
int ComputeDynamicPipeline(int blendMode, bool redAlpha, bool depthBias, bool hud);

// [rc4l] A blend's source and destination factors and its operation, apart from any graphics API.
enum { kBlendFactorSrcAlpha, kBlendFactorInvSrcAlpha, kBlendFactorOne, kBlendFactorDestColor };
enum { kBlendOpAdd, kBlendOpSubtract, kBlendOpRevSubtract };
struct BlendEquation { int src, dst, op; };

// [rc4l] GL's blend for a kWpBlend* value: the source's alpha over the screen, added whole for additive and the subtract styles, and fuzz as GL's DST_COLOR fallback, the screen times the source plus what the source leaves of it.
BlendEquation ComputeBlendEquation(int blend);

// [rc4l] The kWpBlend* a projected decal's pass draws with: fuzz's fallback, additive, or alpha over.
int ComputeDecalPassBlend(bool fuzzPass, bool additivePass);

}} // namespace zx::hwrender

#endif // ZX_WORLDPIPELINES_COMPUTE_H
