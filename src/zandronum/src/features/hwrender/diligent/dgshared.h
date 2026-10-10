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
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; vec4 uFrame; };\n"

// [rc4l] One piece's record as every shader reads it, declared once so its stride cannot drift from ScenePieceData's.
#define FUA_PIECEDATA_GLSL \
	"struct PieceData { vec4 colorLight; vec4 fogAlphaMat; vec4 fogColor; vec4 normal; vec4 floodU; vec4 floodV; vec4 actorLight; vec4 bright; };\n" \
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

namespace zx { namespace hwrender {

Diligent::IRenderDevice  *GetDevice();
Diligent::ISwapChain     *GetSwapChain();

// [rc4l] One GLSL stage compiled as written, or null when it fails (Diligent says why through its message callback).
Diligent::RefCntAutoPtr<Diligent::IShader> CompileShader(Diligent::SHADER_TYPE type, const char *name, const char *source);

// The texture a material draws with, and a resource binding for one against a given pipeline. Both
// cache, so asking every frame is what they are for.
Diligent::ITextureView          *GetMaterialSRV(const void *materialPtr, int translation);
Diligent::IShaderResourceBinding *GetMaterialSRB(Diligent::IPipelineState *pso, const void *material,
                                                 int translation);

// [rc4l] The G-buffer the world pass wrote: what is in front of each pixel, and which way it faces.
// Null before the first frame, or when the pass that fills them is disabled.
Diligent::ITextureView *SceneDepthSRV();
Diligent::ITextureView *SceneNormalSRV();
Diligent::ITextureView *SceneDepthDSV();

// The scene's own constant buffer -- uMVP and friends -- and this frame's view-projection, which a
// pass needs in order to invert it and turn a depth sample back into a world position.
Diligent::IBuffer *SceneConstantsCB();
const float       *SceneMVP();

bool InvertMatrix4(const float *m, float *out);

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

}} // namespace zx::hwrender

#endif // ZX_DGSHARED_H
