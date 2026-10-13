// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The handful of things a second pass needs from the scene renderer.
//
// dgscene.cpp owns the device, the swapchain, the scene's constant buffer, the view matrix and the
// depth and normal targets. A pass that runs inside the same frame -- the decals do -- needs to read
// all of them, and the alternative to naming them here was for it to live inside dgscene.cpp, which
// is how that file got to four thousand lines.
//
// Deliberately small: everything on this list is READ by another pass, never written. Anything that
// wants to change the scene's state belongs in dgscene.cpp with the rest of it.

#ifndef ZX_DGSHARED_H
#define ZX_DGSHARED_H

// [rc4l] The scene constant block every pass binds, declared once; uFrame.x is the warp clock in seconds.
#define FUA_CONSTANTS_GLSL \
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; vec4 uFrame; vec4 uFixedColormap; vec4 uFixedRange; };\n"

// [rc4l] main.fp's fixed colormap on a lit texel, after a block declaring uFixedColormap and uFixedRange: mode 1 (w) maps its grey into the ramp, as invulnerability inverts, and mode 2 multiplies it, as light amplification and a torch tint.
#define FUA_FIXED_COLORMAP_GLSL \
	"vec3 fuaFixedColormap(vec3 c) {\n" \
	"    if (uFixedColormap.w < 1.5) return clamp(uFixedColormap.rgb + dot(c, vec3(0.3, 0.56, 0.14)) * uFixedRange.rgb, 0.0, 1.0);\n" \
	"    return c * uFixedColormap.rgb;\n" \
	"}\n"

// [rc4l] A line mirror's pass drops every fragment at world position p behind the glass; uClipPlane.w of 0 means no mirror.
#define FUA_MIRROR_CLIP_GLSL(p) \
	"    if (uClipPlane.w != 0.0 && dot(" p ", uClipPlane.xyz) + uClipPlane.w < 0.0) discard;\n"

// [rc4l] One piece's record as every shader reads it, declared once so its stride cannot drift from ScenePieceData's.
#define FUA_PIECEDATA_GLSL \
	"struct PieceData { vec4 colorLight; vec4 fogAlphaMat; vec4 fogColor; vec4 normal; vec4 floodU; vec4 floodV; vec4 actorLight; vec4 bright; vec4 extra; };\n" \
	"layout(std430, binding = 9) readonly buffer Pieces { PieceData pieces[]; };\n"

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "Buffer.h"
#include "TextureView.h"
#include "Sampler.h"
#include "PipelineState.h"
#include "ShaderResourceBinding.h"
#include "Shader.h"
#include "RefCntAutoPtr.hpp"

class FString;

namespace zx { namespace hwrender {

Diligent::IRenderDevice  *GetDevice();
Diligent::ISwapChain     *GetSwapChain();
// [rc4l] Where the frame is drawn (dgtarget.cpp): beside GL the window's back buffer, without GL an offscreen target at the screen's size, which FinishFrame draws up to the window before the present; its texture is what a screenshot, a replay or a wipe reads.
bool FrameOffscreen();
Diligent::ITextureView *FrameRTV();
Diligent::ITextureView *FrameDSV();
Diligent::ITexture *FrameTexture();
int FrameWidth();
int FrameHeight();
void FinishFrame(Diligent::IDeviceContext *ctx);
void ReleaseFrameTarget();

// [rc4l] GL draws its views with GL_DEPTH_CLAMP, keeping a surface nearer than the near plane at the near depth instead of cutting it away; a pipeline drawing the world asks for the same when the device can.
void ClampDepthLikeGL(Diligent::GraphicsPipelineDesc &gp);

// [rc4l] One GLSL stage compiled as written, or null when it fails (Diligent says why through its message callback).
Diligent::RefCntAutoPtr<Diligent::IShader> CompileShader(Diligent::SHADER_TYPE type, const char *name, const char *source);

// The texture a material draws with, and a resource binding for one against a given pipeline. Both
// cache, so asking every frame is what they are for.
Diligent::ITextureView          *GetMaterialSRV(const void *materialPtr, int translation);
Diligent::IShaderResourceBinding *GetMaterialSRB(Diligent::IPipelineState *pso, const void *material,
                                                 int translation = 0);

// [rc4l] The G-buffer the world pass wrote: what is in front of each pixel, and which way it faces.
// Null before the first frame, or when the pass that fills them is disabled.
Diligent::ITextureView *SceneDepthSRV();
Diligent::ITextureView *SceneNormalSRV();
Diligent::ITextureView *SceneDepthDSV();

// The scene's own constant buffer -- uMVP and friends -- and this frame's view-projection, which a
// pass needs in order to invert it and turn a depth sample back into a world position.
Diligent::IBuffer *SceneConstantsCB();
const float       *SceneMVP();

// [rc4l] A shader-readable structured buffer of count elements of stride bytes in out, filled from data when given; false when the device refuses it.
bool CreateStructuredBuffer(const char *name, unsigned stride, unsigned long long count, const void *data,
	Diligent::RefCntAutoPtr<Diligent::IBuffer> &out);

// [rc4l] Bind the scene constants on every stage of pso that declares them; false when its vertex stage does not.
bool BindSceneConstants(Diligent::IPipelineState *pso);


// The engine's texture filter, as a sampler. Shared so a second pass cannot quietly disagree with
// the world about how textures are filtered.
void FillSamplerFromEngine(Diligent::SamplerDesc &samp);

// [rc4l] Drop the horizon and decal passes' pipelines, so the next draw builds them with the engine's current filter.
void ReleaseHorizonPipeline();
void ReleaseDecalPipelines();

// [rc4l] While the sky portal pass is drawing: where its camera stands and how far it may reach.
bool SkyPortalReach(float &camX, float &camY, float &reach);

// [rc4l] The horizon windows' planes, drawn after the opaque world -- see features/horizon.
void DrawHorizonPlanes(Diligent::IDeviceContext *ctx);

// [rc4l] Uploads count vertices of stride bytes into a grow-only vertex buffer, re-created when short; false if none can be had.
bool UploadGrowingVB(Diligent::IDeviceContext *ctx, Diligent::RefCntAutoPtr<Diligent::IBuffer> &vb,
	unsigned int &capacity, const void *data, unsigned int count, unsigned int stride, unsigned int pad, const char *name);

// [rc4l] The 2D pass (dg2d.cpp): the captured HUD, menus and console over the frame, and what it drew last.
void Draw2D(Diligent::IDeviceContext *ctx);
void Get2DStats(int &quads, int &draws);

// [rc4l] The light pass (dglights.cpp): its buffers, created once and bound statically to every world pipeline, the per-tic light list, the per-pass cluster grid and its counters.
bool EnsureLightBuffers(FString &err);
void BindLightBuffers(Diligent::IPipelineState *pso);
void CollectDynamicLights(Diligent::IDeviceContext *ctx);
void BuildLightClusters(Diligent::IDeviceContext *ctx);
int LightCount();
int ClusterCells();
void AppendLightStats(FString &report);

}} // namespace zx::hwrender

#endif // ZX_DGSHARED_H
